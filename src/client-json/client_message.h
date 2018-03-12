#ifndef CLIENT_MESSAGE_HH
#define CLIENT_MESSAGE_HH

#include <cstddef>
#include <string>
#include <vector>
#include <exception>

/* Sent by the client to start streaming */
typedef struct ClientInit {
  std::string channel;

  int player_width;
  int player_height;
} ClientInit;

enum PlayerEvent
{
  Unknown,
  Timer,
  Rebuffer,
  CanPlay
};

enum PlayerReadyState
{
  HaveNothing = 0,
  HaveMetadata = 1,
  HaveCurrentData = 2,
  HaveFutureData = 3,
  HaveEnoughData = 4
};

/* Sent by the client when playing */
typedef struct ClientInfo {
  PlayerEvent event;

  double video_buffer_len;
  double audio_buffer_len;

  unsigned int next_video_timestamp;
  unsigned int next_audio_timestamp;

  int player_width;
  int player_height;
  PlayerReadyState player_ready_state;
} ClientInfo;

class ParseExeception: public std::exception
{
public:
    explicit ParseExeception(const char* message): msg_(message) {}
    explicit ParseExeception(const std::string& message): msg_(message) {}
    virtual ~ParseExeception() throw () {}
    virtual const char* what() const throw () {
       return msg_.c_str();
    }
protected:
    std::string msg_;
};

/* Client message format: 
 *   "<message_type> <json string>" 
 */

/* Sent by the client on WS connect to request a channel */
ClientInit parse_client_init_msg(const std::string & data);

/* Sent by the client to inform the server's decisions */
ClientInfo parse_client_info_msg(const std::string & data);


/* Server message format:
 *  [0:4]             message _en (network endian)
 *  [4:4+message_len] json string
 *  [4+message_len:]  data
 */

/* Message sent on initial WS connect */
std::vector<std::byte> make_server_hello_msg(const std::vector<std::string> & channels);

/* Message sent to reinitialize the client's sourcebuffer */
std::vector<std::byte> make_server_init_msg(
  const std::string & channel, 
  const std::string & video_codec,
  const std::string & audio_codec,
  const unsigned int & timescale);

/* Audio chunk message, payload contains the init and data */
std::vector<std::byte> make_audio_msg(
  const std::string & quality,
  const unsigned int & timestamp,
  const unsigned int & duration,
  const unsigned int & byte_offset,
  const unsigned int & total_byte_length);

/* Video chunk message, payload contains the init and data */
std::vector<std::byte> make_video_msg(
  const std::string & quality,
  const unsigned int & timestamp,
  const unsigned int & duration,
  const unsigned int & byte_offset,
  const unsigned int & total_byte_length);

#endif /* CLIENT_MESSAGE_HH */