
#include <cstddef>
#include <vector>
#include <string>
#include <iostream>

#include "client_message.h"
#include "json.hpp"

using namespace std;
using json = nlohmann::json;

void print_msg(vector<byte> msg) {
  for (int i = sizeof(uint32_t); i < msg.size(); i++) {
    cout << static_cast<char>(msg[i]);
  }
  cout << endl;
}

int main(int argc, char * argv[]) {

  /* Test server message generation */
  vector<string> channels({"pbs", "nbc"});
  
  print_msg(make_server_hello_msg(channels));
  print_msg(make_server_init_msg("pbs", "vcodec", "acodec", 180000));
  print_msg(make_audio_msg("128k", 0, 480000, 0, 1000));
  print_msg(make_video_msg("1920x1080-20", 0, 180000, 0, 1000));

  /* Test client message parsing */
  json client_init = {
    {"channel", "pbs"},
    {"playerWidth", 1920},
    {"playerHeight", 1080}
  };
  parse_client_init_msg(client_init.dump());

  json client_info = {
    {"event", "timer"},
    {"videoBufferLen", 10.0},
    {"audioBufferLen", 10.0},
    {"nextVideoTimestamp", 0},
    {"nextAudioTimestamp", 0},
    {"playerWidth", 1920},
    {"playerHeight", 1080},
    {"playerReadyState", 0}
  };
  parse_client_info_msg(client_info.dump());

  return 0;
}