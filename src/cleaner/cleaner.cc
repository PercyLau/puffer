#include <iostream>
#include <string>
#include <getopt.h>
#include <sys/types.h>
#include <dirent.h>
#include <regex>

using namespace std;

void print_usage(char * program_name)
{
  cerr << "Usage: " << program_name << " [options]" << endl;
  cerr << "\t-d <dir> -dir=<dir>        clean everything in <dir>" << endl;
  cerr << "\t-p <patt> -pattern=<patt>  file name with <patt> will be cleaned.\
 <patt> is a regular expression."
    << endl;
}

const char *optstring = "d:p:";
const struct option options[]={
  {"dir",required_argument,NULL,'d'},
  {"pattern",required_argument,NULL,'p'},
  {NULL,0,NULL,0}
};

int main(int argc, char * *argv)
{
  int c;
  int long_option_index;
  char * dir_name = NULL;
  char * pattern = NULL;
  cmatch m;

  if(argc != 5) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  while((c=getopt_long(argc,argv,optstring,options,&long_option_index))!=EOF){
    switch(c){
      case 'd': dir_name = optarg; break;
      case 'p': pattern = optarg; break;
      default: {
                print_usage(argv[0]);
                return EXIT_FAILURE;
               }
    }
  }

  if(dir_name == NULL || pattern == NULL) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  /* compile regex */
  regex re_file(pattern);

  DIR *directory = opendir(dir_name);

  if(directory == NULL)
  {
    cerr << "Unable to open directory " << dir_name;
    return EXIT_FAILURE;
  }

  struct dirent *entry;
  while(NULL != ( entry = readdir(directory)))
  {
    char * filename = entry->d_name;
    if(regex_match(filename, m, re_file)) {
      string fullpath = string(dir_name) + "/" + string(filename);
      /* delete files */
      if(remove(fullpath.c_str()))
        cerr << "Unable to delete file " << fullpath << endl;
      else
        cout << fullpath << " deleted" << endl;
    }
  }

  return EXIT_SUCCESS;
}