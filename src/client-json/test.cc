
#include <cstddef>
#include <vector>
#include <string>
#include <iostream>
#include <arpa/inet.h>

#include "client_message.h"
#include "json.hpp"

using namespace std;
using json = nlohmann::json;

void print_msg(vector<byte> msg) {
  uint16_t msg_len;
  memcpy(&msg_len, &msg[0], sizeof(uint16_t));
  msg_len = ntohs(msg_len);
  if (msg_len != msg.size() - sizeof(uint16_t)) {
    cout << "Message has incorrect length: " << msg_len << endl;
    abort();
  }
  for (int i = sizeof(uint16_t); i < msg.size(); i++) {
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

  /* Test some examples that should fail */
  try { /* Empty object */
    parse_client_init_msg("{}");
    abort();
  } catch (const ParseExeception & e) {
    cout << "Ok: " << e.what() << endl;
  }

  try { /* [] instead of {} */
    parse_client_init_msg("[]");
    abort();
  } catch (const ParseExeception & e) {
    cout << "Ok: " << e.what() << endl;
  }

  try { /* Malformed JSON */
    parse_client_init_msg("{\"channel\":\"pbs\",\"playerWidth\":1280,\"playerHeight\":720");
    abort();
  } catch (const ParseExeception & e) {
    cout << "Ok: " << e.what() << endl;
  }

  try {
    parse_client_info_msg("{}");
    abort();
  } catch (const ParseExeception & e) {
    cout << "Ok: " << e.what() << endl;
  }

  return 0;
}