/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "config.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
#include <cstring>
#include <array>
#include <queue>
#include <optional>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef HAVE_STRING_VIEW
#include <string_view>
#elif HAVE_EXPERIMENTAL_STRING_VIEW
#include <experimental/string_view>
using std::experimental::string_view;
#endif

extern "C" {
#include "mpeg2.h"
}
 
#include "file_descriptor.hh"
#include "exception.hh"

using namespace std;

const size_t ts_packet_length = 188;
const char ts_packet_sync_byte = 0x47;
const size_t packets_in_chunk = 512;

template <typename T>
inline T * notnull( const string & context, T * const x )
{
  return x ? x : throw runtime_error( context + ": returned null pointer" );
}

uint64_t timestamp_difference( const uint64_t ts_64, const uint64_t ts_33 )
{
  const uint64_t second_ts_unwrapped = (ts_64 & 0xffffffff80000000) | (ts_33 & 0x000000007fffffff);

  const int64_t first_ts_signed = ts_64;
  const int64_t second_ts_signed = second_ts_unwrapped;

  return abs( first_ts_signed - second_ts_signed );
}

struct Raster
{
  unsigned int width, height;
  unique_ptr<uint8_t[]> Y, Cb, Cr;

  Raster( const unsigned int s_width,
          const unsigned int s_height )
    : width( s_width ),
      height( s_height ),
      Y(  make_unique<uint8_t[]>(  height    *  width ) ),
      Cb( make_unique<uint8_t[]>( (height/2) * (width/2) ) ),
      Cr( make_unique<uint8_t[]>( (height/2) * (width/2) ) )
  {
    if ( (height % 2 != 0)
         or (width % 2 != 0) ) {
      throw runtime_error( "width or height is not multiple of 2" );
    }
  }
};

class non_fatal_exception : public runtime_error
{
public:
  using runtime_error::runtime_error;
};

class InvalidMPEG : public non_fatal_exception
{
public:
  using non_fatal_exception::non_fatal_exception;
};

class UnsupportedMPEG : public non_fatal_exception
{
public:
  using non_fatal_exception::non_fatal_exception;
};

class StreamMismatch : public non_fatal_exception
{
public:
  using non_fatal_exception::non_fatal_exception;
};

struct FieldBuffer : public Raster
{
  using Raster::Raster;

  void read_from_frame( const bool top_field,
                        const unsigned int physical_luma_width,
                        const mpeg2_fbuf_t * display_raster )
  {
    if ( physical_luma_width < width ) {
      throw runtime_error( "invalid physical_luma_width" );
    }

    /* copy Y */
    for ( unsigned int source_row = (top_field ? 0 : 1), dest_row = 0;
          dest_row < height;
          source_row += 2, dest_row += 1 ) {
      memcpy( Y.get() + dest_row * width,
              display_raster->buf[ 0 ] + source_row * physical_luma_width,
              width );
    }

    /* copy Cb */
    for ( unsigned int source_row = (top_field ? 0 : 1), dest_row = 0;
          dest_row < height/2;
          source_row += 2, dest_row += 1 ) {
      memcpy( Cb.get() + dest_row * width/2,
              display_raster->buf[ 1 ] + source_row * physical_luma_width/2,
              width/2 );
    }

    /* copy Cr */
    for ( unsigned int source_row = (top_field ? 0 : 1), dest_row = 0;
          dest_row < height/2;
          source_row += 2, dest_row += 1 ) {
      memcpy( Cr.get() + dest_row * width/2,
              display_raster->buf[ 2 ] + source_row * physical_luma_width/2,
              width/2 );
    }
  }
};

class FieldBufferPool;

class FieldBufferDeleter
{
private:
  FieldBufferPool * buffer_pool_ = nullptr;

public:
  void operator()( FieldBuffer * buffer ) const;

  void set_buffer_pool( FieldBufferPool * pool );
};

typedef unique_ptr<FieldBuffer, FieldBufferDeleter> FieldBufferHandle;

class FieldBufferPool
{
private:
  queue<FieldBufferHandle> unused_buffers_ {};

public:
  FieldBufferHandle make_buffer( const unsigned int luma_width,
                                 const unsigned int field_luma_height )
  {
    FieldBufferHandle ret;

    if ( unused_buffers_.empty() ) {
      ret.reset( new FieldBuffer( luma_width, field_luma_height ) );
    } else {
      if ( (unused_buffers_.front()->width != luma_width)
           or (unused_buffers_.front()->height != field_luma_height) ) {
        throw runtime_error( "buffer size has changed" );
      }

      ret = move( unused_buffers_.front() );
      unused_buffers_.pop();
    }

    ret.get_deleter().set_buffer_pool( this );
    return ret;
  }

  void free_buffer( FieldBuffer * buffer )
  {
    if ( not buffer ) {
      throw runtime_error( "attempt to free null buffer" );
    }

    unused_buffers_.emplace( buffer );
  }
};

void FieldBufferDeleter::operator()( FieldBuffer * buffer ) const
{
  if ( buffer_pool_ ) {
    buffer_pool_->free_buffer( buffer );
  } else {
    delete buffer;
  }
}

void FieldBufferDeleter::set_buffer_pool( FieldBufferPool * pool )
{
  if ( buffer_pool_ ) {
    throw runtime_error( "buffer_pool already set" );
  }

  buffer_pool_ = pool;
}

FieldBufferPool & global_buffer_pool()
{
  static FieldBufferPool pool;
  return pool;
}

struct VideoField
{
  uint64_t presentation_time_stamp;
  bool top_field;
  FieldBufferHandle contents;

  VideoField( const uint64_t presentation_time_stamp,
              const bool top_field,
              const unsigned int luma_width,
              const unsigned int frame_luma_height,
              const unsigned int physical_luma_width,
              const mpeg2_fbuf_t * display_raster )
    : VideoField( presentation_time_stamp,
                  top_field,
                  luma_width,
                  frame_luma_height )
  {
    contents->read_from_frame( top_field, physical_luma_width, display_raster );
  }

  VideoField( const uint64_t presentation_time_stamp,
              const bool top_field,
              const unsigned int luma_width,
              const unsigned int frame_luma_height )
    : presentation_time_stamp( presentation_time_stamp ),
      top_field( top_field ),
      contents( global_buffer_pool().make_buffer( luma_width, frame_luma_height / 2 ) )
  {}
};

struct TSPacketRequirements
{
  TSPacketRequirements( const string_view & packet )
  {
    /* enforce invariants */
    if ( packet.length() != ts_packet_length ) {
      throw InvalidMPEG( "invalid TS packet length" );
    }

    if ( packet.front() != ts_packet_sync_byte ) {
      throw InvalidMPEG( "invalid TS sync byte" );
    }
  }
};

struct TSPacketHeader : TSPacketRequirements
{
  bool transport_error_indicator;
  bool payload_unit_start_indicator;
  uint16_t pid;
  uint8_t adaptation_field_control;
  uint8_t payload_start;

  TSPacketHeader( const string_view & packet )
    : TSPacketRequirements( packet ),
      transport_error_indicator( packet[ 1 ] & 0x80 ),
      payload_unit_start_indicator( packet[ 1 ] & 0x40 ),
      pid( ((uint8_t( packet[ 1 ] ) & 0x1f) << 8) | uint8_t( packet[ 2 ] ) ),
      adaptation_field_control( (uint8_t( packet[ 3 ] ) & 0x30) >> 4 ),
      payload_start( 4 )
  {
    /* find start of payload */
    switch ( adaptation_field_control ) {
    case 0:
      throw UnsupportedMPEG( "reserved value of adaptation field control" );
    case 1:
      /* already 4 */
      break;
    case 2:
      payload_start = ts_packet_length; /* no data */
      break;
    case 3:
      const uint8_t adaptation_field_length = packet[ 4 ];
      payload_start += adaptation_field_length + 1 /* length field is 1 byte itself */;
      break;
    }

    if ( payload_start > ts_packet_length ) {
      throw InvalidMPEG( "invalid TS packet" );
    }
  }
};

struct TimestampedPESPacket
{
  uint64_t presentation_time_stamp;
  size_t payload_start_index;
  size_t payload_end_index;
  string PES_packet;

  TimestampedPESPacket( const uint64_t s_presentation_time_stamp,
                        const size_t s_payload_start_index,
                        const size_t PES_packet_length,
                        string && s_PES_packet )
    : presentation_time_stamp( s_presentation_time_stamp ),
      payload_start_index( s_payload_start_index ),
      payload_end_index( s_PES_packet.size() ),
      PES_packet( move( s_PES_packet ) )
  {
    if ( payload_start_index >= PES_packet.size() ) {
      throw InvalidMPEG( "empty PES payload" );
    }

    if ( PES_packet_length != 0 ) {
      if ( PES_packet_length + 6 > PES_packet.size() ) {
        throw InvalidMPEG( "PES_packet_length + 6 > PES_packet.size()" );
      }

      payload_end_index = PES_packet_length + 6;
    }
  }

  uint8_t * payload_start()
  {
    return reinterpret_cast<uint8_t *>( PES_packet.data() + payload_start_index );
  }

  uint8_t * payload_end()
  {
    return reinterpret_cast<uint8_t *>( PES_packet.data() + payload_end_index );
  }
};

struct PESPacketHeader
{
  uint8_t stream_id;
  unsigned int payload_start;
  unsigned int PES_packet_length;
  bool data_alignment_indicator;
  uint8_t PTS_DTS_flags;
  uint64_t presentation_time_stamp;
  uint64_t decoding_time_stamp;

  static uint8_t enforce_stream_id( const bool is_video, const uint8_t stream_id )
  {
    if ( is_video ) {
      if ( (stream_id & 0xf0) != 0xe0 ) {
        throw StreamMismatch( "not an MPEG-2 video stream: " + to_string( stream_id ) );
      }
    } else {
      if ( stream_id != 0xBD ) {
        throw StreamMismatch( "not an A/52 audio stream: " + to_string( stream_id ) );
      }
    }

    return stream_id;
  }

  PESPacketHeader( const string_view & packet, const bool is_video )
    : stream_id( enforce_stream_id( is_video, packet.at( 3 ) ) ),
      payload_start( packet.at( 8 ) + 9 ),
      PES_packet_length( (packet.at( 4 ) << 8) | packet.at( 5 ) ),
      data_alignment_indicator( packet.at( 6 ) & 0x04 ),
      PTS_DTS_flags( (packet.at( 7 ) & 0xc0) >> 6 ),
      presentation_time_stamp(),
      decoding_time_stamp()
  {
    if ( packet.at( 0 ) != 0
         or packet.at( 1 ) != 0
         or packet.at( 2 ) != 1 ) {
      throw InvalidMPEG( "invalid PES start code" );
    }

    if ( payload_start > packet.length() ) {
      throw InvalidMPEG( "invalid PES packet" );
    }

    /*
      unfortunately NBC San Francisco does not seem to use this
    if ( not data_alignment_indicator ) {
      throw runtime_error( "unaligned PES packet" );
    }
    */

    switch ( PTS_DTS_flags ) {
    case 0:
      throw UnsupportedMPEG( "missing PTS and DTS" );
    case 1:
      throw InvalidMPEG( "forbidden value of PTS_DTS_flags" );
    case 3:
      decoding_time_stamp = (((uint64_t(uint8_t(packet.at( 14 )) & 0x0F) >> 1) << 30) |
                             (uint8_t(packet.at( 15 )) << 22) |
                             ((uint8_t(packet.at( 16 )) >> 1) << 15) |
                             (uint8_t(packet.at( 17 )) << 7) |
                             (uint8_t(packet.at( 18 )) >> 1));

      if ( (packet.at( 14 ) & 0xf0) >> 4 != 1 ) {
        throw InvalidMPEG( "invalid DTS prefix bits" );
      }

      if ( (packet.at( 14 ) & 0x01) != 1 ) {
        throw InvalidMPEG( "invalid marker bit" );
      }

      if ( (packet.at( 16 ) & 0x01) != 1 ) {
        throw InvalidMPEG( "invalid marker bit" );
      }

      if ( (packet.at( 18 ) & 0x01) != 1 ) {
        throw InvalidMPEG( "invalid marker bit" );
      }

      /* fallthrough */

    case 2:
      presentation_time_stamp = (((uint64_t(uint8_t(packet.at( 9 )) & 0x0F) >> 1) << 30) |
                                 (uint8_t(packet.at( 10 )) << 22) |
                                 ((uint8_t(packet.at( 11 )) >> 1) << 15) |
                                 (uint8_t(packet.at( 12 )) << 7) |
                                 (uint8_t(packet.at( 13 )) >> 1));

      if ( (packet.at( 9 ) & 0xf0) >> 4 != PTS_DTS_flags ) {
        throw InvalidMPEG( "invalid PTS prefix bits" );
      }

      if ( (packet.at( 9 ) & 0x01) != 1 ) {
        throw InvalidMPEG( "invalid marker bit" );
      }

      if ( (packet.at( 11 ) & 0x01) != 1 ) {
        throw InvalidMPEG( "invalid marker bit" );
      }

      if ( (packet.at( 13 ) & 0x01) != 1 ) {
        throw InvalidMPEG( "invalid marker bit" );
      }
    }

    if ( PTS_DTS_flags == 2 ) {
      decoding_time_stamp = presentation_time_stamp;
    }
  }
};

struct VideoParameters
{
  unsigned int width {};
  unsigned int height {};
  unsigned int frame_interval {};
  string y4m_description {};
  bool progressive {};

  VideoParameters( const string & format )
  {
    if ( format == "1080i30" ) {
      width = 1920;
      height = 1080;
      frame_interval = 900900;
      y4m_description = "F30000:1001 It";
      progressive = false;
    } else if ( format == "720p60" ) {
      width = 1280;
      height = 720;
      frame_interval = 450450;
      y4m_description = "F60000:1001 Ip";
      progressive = true;
    } else {
      throw UnsupportedMPEG( "unsupported format: " + format );
    }
  }
};

class MPEG2VideoDecoder
{
private:
  struct MPEG2Deleter
  {
    void operator()( mpeg2dec_t * const x ) const
    {
      mpeg2_close( x );
    }
  };

  unique_ptr<mpeg2dec_t, MPEG2Deleter> decoder_;

  unsigned int display_width_;
  unsigned int display_height_;
  unsigned int frame_interval_;
  bool progressive_sequence_;

  static unsigned int macroblock_dimension( const unsigned int num )
  {
    return ( num + 15 ) / 16;
  }

    unsigned int physical_luma_width() const
  {
    return macroblock_dimension( display_width_ ) * 16;
  }

  unsigned int physical_luma_height() const
  {
    return macroblock_dimension( display_height_ ) * 16;
  }

  void enforce_as_expected( const mpeg2_sequence_t * sequence ) const
  {
    if ( not (sequence->flags & SEQ_FLAG_MPEG2) ) {
      throw InvalidMPEG( "sequence not flagged as MPEG-2 part 2 video" );
    }

    if ( (sequence->flags & SEQ_FLAG_PROGRESSIVE_SEQUENCE) != (SEQ_FLAG_PROGRESSIVE_SEQUENCE * progressive_sequence_) ) {
      throw StreamMismatch( "progressive/interlaced sequence mismatch" );
    }

    if ( sequence->width != physical_luma_width() ) {
      throw StreamMismatch( "width mismatch" );
    }

    if ( sequence->height != physical_luma_height() ) {
      throw StreamMismatch( "height mismatch" );
    }

    if ( sequence->chroma_width != physical_luma_width() / 2 ) {
      throw StreamMismatch( "chroma width mismatch" );
    }

    if ( sequence->chroma_height != physical_luma_height() / 2 ) {
      throw StreamMismatch( "chroma height mismatch" );
    }

    if ( sequence->picture_width != display_width_ ) {
      throw StreamMismatch( "picture width mismatch" );
    }

    if ( sequence->picture_height != display_height_ ) {
      throw StreamMismatch( "picture height mismatch" );
    }

    if ( sequence->display_width != display_width_ ) {
      throw StreamMismatch( "display width mismatch" );
    }

    if ( sequence->display_height != display_height_ ) {
      throw StreamMismatch( "display height mismatch" );
    }

    if ( sequence->pixel_width != 1
         or sequence->pixel_height != 1 ) {
      throw UnsupportedMPEG( "non-square pels" );
    }
    
    if ( sequence->frame_period != frame_interval_ ) {
      throw StreamMismatch( "frame interval mismatch" );
    }
  }

  void output_picture( const mpeg2_picture_t * pic,
                       const mpeg2_fbuf_t * display_raster,
                       queue<VideoField> & output )
  {
    if ( not (pic->flags & PIC_FLAG_TAGS) ) {
      throw UnsupportedMPEG( "picture without timestamp" );
    }

    if ( progressive_sequence_ ) {
      if ( pic->nb_fields % 2 != 0 ) {
        throw runtime_error( "progressive sequence, but picture has odd number of fields" );
      }
    }

    bool next_field_is_top = (pic->flags & PIC_FLAG_TOP_FIELD_FIRST) | progressive_sequence_;
    uint64_t presentation_time_stamp_27M = 300 * ( (uint64_t( pic->tag ) << 32) | (pic->tag2) );

    /* output each field */
    for ( unsigned int field = 0; field < pic->nb_fields; field++ ) {
      output.emplace( presentation_time_stamp_27M,
                      next_field_is_top,
                      display_width_,
                      display_height_,
                      physical_luma_width(),
                      display_raster );

      next_field_is_top = !next_field_is_top;
      presentation_time_stamp_27M += frame_interval_ / 2; /* treat all as interlaced */
    }
  }

public:
  MPEG2VideoDecoder( const VideoParameters & params )
    : decoder_( notnull( "mpeg2_init", mpeg2_init() ) ),
      display_width_( params.width ),
      display_height_( params.height ),
      frame_interval_( params.frame_interval ),
      progressive_sequence_( params.progressive )
  {
    if ( (display_width_ % 4 != 0)
         or (display_height_ % 4 != 0) ) {
      throw runtime_error( "width or height is not multiple of 4" );
    }
  }

  void decode_frame( TimestampedPESPacket & PES_packet, /* mutable because mpeg2_buffer args are not const */
                     queue<VideoField> & output )
  {
    mpeg2_tag_picture( decoder_.get(),
                       PES_packet.presentation_time_stamp >> 32,
                       PES_packet.presentation_time_stamp & 0xFFFFFFFF );

    /* give bytes to the MPEG-2 video decoder */
    mpeg2_buffer( decoder_.get(),
                  PES_packet.payload_start(),
                  PES_packet.payload_end() );

    /* actually decode the frame */
    unsigned int picture_count = 0;

    while ( true ) {
      mpeg2_state_t state = mpeg2_parse( decoder_.get() );
      const mpeg2_info_t * decoder_info = notnull( "mpeg2_info",
                                                   mpeg2_info( decoder_.get() ) );
      
      switch ( state ) {
      case STATE_SEQUENCE:
      case STATE_SEQUENCE_REPEATED:
        {
          const mpeg2_sequence_t * sequence = notnull( "sequence",
                                                       decoder_info->sequence );
          enforce_as_expected( sequence );
        }
        break;
      case STATE_BUFFER:
        if ( picture_count > 1 ) {
          throw runtime_error( "PES packet with multiple pictures" );
        }
        return;
      case STATE_SLICE:
        picture_count++;

        {
          const mpeg2_picture_t * pic = decoder_info->display_picture;
          if ( pic ) {
            /* picture ready for display */          
            const mpeg2_fbuf_t * display_raster = notnull( "display_fbuf", decoder_info->display_fbuf );
            output_picture( pic, display_raster, output );
          }
        }
        break;
      case STATE_INVALID:
      case STATE_INVALID_END:
        cerr << "libmpeg2 is in STATE_INVALID\n";
        break;
      case STATE_PICTURE:
        break;
      case STATE_PICTURE_2ND:
        throw UnsupportedMPEG( "unsupported field pictures" );
      default:
        /* do nothing */
        break;
      }
    }
  }
};

class TSParser
{
private:
  unsigned int pid_; /* program ID of interest */
  bool is_video_; /* true = video, false = audio */
  
  string PES_packet_ {};

  void append_payload( const string_view & packet, const TSPacketHeader & header )
  {
    const string_view payload = packet.substr( header.payload_start );
    PES_packet_.append( payload.begin(), payload.end() );
  }

public:
  TSParser( const unsigned int pid, const bool is_video )
    : pid_( pid ),
      is_video_( is_video )
  {
    if ( pid >= (1 << 13) ) {
      throw runtime_error( "program ID must be less than " + to_string( 1 << 13 ) );
    }
  }

  void parse( const string_view & packet, queue<TimestampedPESPacket> & video_PES_packets )
  {
    TSPacketHeader header { packet };

    if ( header.pid != pid_ ) {
      return;
    }

    if ( header.payload_unit_start_indicator ) {
      /* start of new PES packet */

      /* step 1: parse and decode old PES packet if there is one */
      if ( not PES_packet_.empty() ) {
        PESPacketHeader pes_header { PES_packet_, is_video_ };
        
        video_PES_packets.emplace( pes_header.presentation_time_stamp,
                                   pes_header.payload_start,
                                   pes_header.PES_packet_length,
                                   move( PES_packet_ ) );
        PES_packet_.clear();
      }

      /* step 2: start a new PES packet */
      append_payload( packet, header );
    } else if ( not PES_packet_.empty() ) {
      /* interior TS packet within a PES packet */
      append_payload( packet, header );
    }
  }
};

class YUV4MPEGPipeOutput
{
private:
  bool next_field_is_top_ { true };

  unsigned int pending_chunk_outer_timestamp_ {};
  unsigned int pending_chunk_index_ {};
  vector<Raster> pending_chunk_;

  unsigned int frame_interval_;
  optional<uint64_t> expected_inner_timestamp_ {};
  unsigned int outer_timestamp_ {};

  VideoField missing_field_;

  string directory_;
  string y4m_header_;
  
  Raster & pending_frame()
  {
    return pending_chunk_.at( pending_chunk_index_ );
  }

  void write_frame_to_disk()
  {
    if ( pending_chunk_index_ == 0 ) {
      pending_chunk_outer_timestamp_ = outer_timestamp_ / 300;
      cerr << "Starting new chunk with outer timestamp = " << pending_chunk_outer_timestamp_ << "\n";
    }

    if ( pending_chunk_index_ == pending_chunk_.size() - 1 ) {
      const string filename = "video." + to_string( pending_chunk_outer_timestamp_ ) + ".y4m";

      cerr << "Writing " << directory_ + "/" + filename << " ... ";

      FileDescriptor directory_fd_ { CheckSystemCall( "open " + directory_, open( directory_.c_str(),
                                                                                  O_DIRECTORY ) ) };

      FileDescriptor output_ { CheckSystemCall( "openat", openat( directory_fd_.fd_num(),
                                                                  filename.c_str(),
                                                                  O_WRONLY | O_CREAT | O_EXCL,
                                                                  S_IRUSR | S_IWUSR ) ) };

      output_.write( y4m_header_ );
      
      for ( const auto & pending_frame : pending_chunk_ ) {
        output_.write( "FRAME\n" );
        
        /* Y */
        output_.write( string_view { reinterpret_cast<char *>( pending_frame.Y.get() ), pending_frame.width * pending_frame.height } );

        /* Cb */
        output_.write( string_view { reinterpret_cast<char *>( pending_frame.Cb.get() ), (pending_frame.width/2) * (pending_frame.height/2) } );

        /* Cr */
        output_.write( string_view { reinterpret_cast<char *>( pending_frame.Cr.get() ), (pending_frame.width/2) * (pending_frame.height/2) } );
      }

      cerr << "done.\n";
    }
        
    /* advance virtual clock */
    outer_timestamp_ += frame_interval_;

    pending_chunk_index_ = (pending_chunk_index_ + 1) % pending_chunk_.size();
  }

public:
  YUV4MPEGPipeOutput( const string directory,
                      const unsigned int frames_per_chunk,
                      const VideoParameters & params )
    : pending_chunk_(),
      frame_interval_( params.frame_interval ),
      missing_field_( 0, false, params.width, params.height ),
      directory_( directory ),
      y4m_header_( "YUV4MPEG2 W" + to_string( params.width )
                   + " H" + to_string( params.height ) + " " + params.y4m_description
                   + " A1:1 C420mpeg2\n" )
  {
    for ( unsigned int i = 0; i < frames_per_chunk; i++ ) {
      pending_chunk_.emplace_back( params.width, params.height );
    }
  }

  void write( const VideoField & field )
  {
    if ( field.top_field != next_field_is_top_ ) {
      cerr << "ignoring field with mismatched cadence\n";
      return;
    }
    
    if ( expected_inner_timestamp_.has_value() ) {
      const int64_t diff = abs( int64_t( expected_inner_timestamp_.value() ) - int64_t( field.presentation_time_stamp ) );
      if ( timestamp_difference( expected_inner_timestamp_.value(),
                                 field.presentation_time_stamp )
           > 5 * frame_interval_ / 8 ) {
        cerr << "Warning, diff = " << diff << " (" << diff / double( frame_interval_ ) << " frames): ";
        cerr << "expected time stamp of " << expected_inner_timestamp_.value() << " but got " << field.presentation_time_stamp << "\n";
        if ( field.presentation_time_stamp < expected_inner_timestamp_.value() ) {
          if ( diff > frame_interval_ * 60 * 60 ) {
            throw runtime_error( "BUG: huge negative difference (need to reinitialize inner timestamp)" );
            return;
          }
        } else {
          while ( timestamp_difference( expected_inner_timestamp_.value(),
                                        field.presentation_time_stamp )
                  > 5 * frame_interval_ / 8 ) {
            cerr << "Generating replacement fields to fill in gap.\n";

            /* first field */
            missing_field_.presentation_time_stamp = expected_inner_timestamp_.value();
            missing_field_.top_field = next_field_is_top_;
            write_raw( missing_field_ );

            /* second field */
            missing_field_.presentation_time_stamp = expected_inner_timestamp_.value();
            missing_field_.top_field = next_field_is_top_;
            write_raw( missing_field_ );
          }
        }
      }
     } else {
      expected_inner_timestamp_ = field.presentation_time_stamp;
      cerr << "initializing expected inner timestamp = " << expected_inner_timestamp_.value() << "\n";
    }

    write_raw( field );
  }

  void write_raw( const VideoField & field )
  {
    if ( field.top_field != next_field_is_top_ ) {
      throw runtime_error( "field cadence mismatch" );
    }
    
    /* copy field to proper lines of pending frame */
    
    /* copy Y */
    for ( unsigned int source_row = 0, dest_row = (next_field_is_top_ ? 0 : 1);
          source_row < field.contents->height;
          source_row += 1, dest_row += 2 ) {
      memcpy( pending_frame().Y.get() + dest_row * pending_frame().width,
              field.contents->Y.get() + source_row * pending_frame().width,
              pending_frame().width );
    }

    /* copy Cb */
    for ( unsigned int source_row = 0, dest_row = (next_field_is_top_ ? 0 : 1);
          source_row < field.contents->height/2;
          source_row += 1, dest_row += 2 ) {
      memcpy( pending_frame().Cb.get() + dest_row * pending_frame().width/2,
              field.contents->Cb.get() + source_row * pending_frame().width/2,
              pending_frame().width/2 );
    }

    /* copy Cr */
    for ( unsigned int source_row = 0, dest_row = (next_field_is_top_ ? 0 : 1);
          source_row < field.contents->height/2;
          source_row += 1, dest_row += 2 ) {
      memcpy( pending_frame().Cr.get() + dest_row * pending_frame().width/2,
              field.contents->Cr.get() + source_row * pending_frame().width/2,
              pending_frame().width/2 );
    }

    next_field_is_top_ = !next_field_is_top_;
    expected_inner_timestamp_.value() += frame_interval_ / 2;

    if ( next_field_is_top_ ) {
      /* print out frame */
      write_frame_to_disk();
    }
  }
};

int main( int argc, char *argv[] )
{
  try {
    if ( argc < 1 ) { /* for pedants */
      abort();
    }

    if ( argc != 6 ) {
      cerr << "Usage: " << argv[ 0 ] << " video_pid audio_pid format frames_per_chunk target_directory\n\n   format = \"1080i30\" | \"720p60\"\n";
      return EXIT_FAILURE;
    }

    /* NB: "1080i30" is the preferred notation in Poynton's books and "Video Demystified" */
    const unsigned int video_pid = stoi( argv[ 1 ] );
    const unsigned int audio_pid = stoi( argv[ 2 ] );
    const VideoParameters params { argv[ 3 ] };
    const unsigned int frames_per_chunk = atoi( argv[ 4 ] );
    const string directory = argv[ 5 ];
    
    FileDescriptor stdin { 0 };

    TSParser video_parser { video_pid, true };
    TSParser audio_parser { audio_pid, false };
    queue<TimestampedPESPacket> video_PES_packets; /* output of TSParser */
    queue<TimestampedPESPacket> audio_PES_packets; /* output of TSParser */

    MPEG2VideoDecoder video_decoder { params };
    queue<VideoField> decoded_fields; /* output of MPEG2VideoDecoder */

    YUV4MPEGPipeOutput y4m_output { directory, frames_per_chunk, params };

    FileDescriptor output_ { STDOUT_FILENO };
    
    while ( true ) {
      /* parse transport stream packets into video (and eventually audio) PES packets */
      const string chunk = stdin.read_exactly( ts_packet_length * packets_in_chunk );
      const string_view chunk_view { chunk };

      for ( unsigned packet_no = 0; packet_no < packets_in_chunk; packet_no++ ) {
        try {
          video_parser.parse( chunk_view.substr( packet_no * ts_packet_length,
                                                 ts_packet_length ),
                              video_PES_packets );
          audio_parser.parse( chunk_view.substr( packet_no * ts_packet_length,
                                                 ts_packet_length ),
                              audio_PES_packets );
        } catch ( const non_fatal_exception & e ) {
          print_exception( "transport stream input", e );
        }
      }

      /* decode video */
      while ( not video_PES_packets.empty() ) {
        try {
          TimestampedPESPacket PES_packet { move( video_PES_packets.front() ) };
          video_PES_packets.pop();
          video_decoder.decode_frame( PES_packet, decoded_fields );
        } catch ( const non_fatal_exception & e ) {
          print_exception( "video decode", e );
        }
      }

      /* decode audio */
      while ( not audio_PES_packets.empty() ) {
        try {
          TimestampedPESPacket PES_packet { move( audio_PES_packets.front() ) };
          audio_PES_packets.pop();
          unsigned int payload_len = PES_packet.payload_end() - PES_packet.payload_start();
          string_view audio { reinterpret_cast<char *>( PES_packet.payload_start() ), payload_len };
          output_.write( audio );
        } catch ( const non_fatal_exception & e ) {
          print_exception( "audio decode", e );
        }
      }
      
      /* output fields? */
      while ( not decoded_fields.empty() ) {
        y4m_output.write( decoded_fields.front() );
        decoded_fields.pop();
      }
    }
  } catch ( const exception & e ) {
    print_exception( argv[ 0 ], e );
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
