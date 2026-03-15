#include <iostream>
#include <string>
#include "include/tools/CommandLineTool.h"
#include "include/tools/argparse.hpp"

using namespace std;

int main(int argc,const char **argv) {
    ArgumentParser parser;

    parser.addArgument("-i", "--input_file", 1, true);
    parser.addArgument("-r", "--resource_dir", 1, true);
    parser.addArgument("-m", "--mode", 1, true);
    parser.addArgument("-h", "--help", 0, true);

    parser.useExceptions(true);
    try {
        parser.parse(argc, argv);
    } catch (const std::exception& e) {
        cerr << "Error parsing arguments: " << e.what() << endl;
        cerr << parser.usage() << endl;
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            cout << parser.usage() << endl;
            return 0;
        }
    }

    if (argc == 1) {
        cout << parser.usage() << endl;
        return 0;
    }

    string input_file = parser.retrieve<string>("input_file");
    string resource_dir = parser.retrieve<string>("resource_dir");
    if(resource_dir.empty()){
        resource_dir = "./resources";
    }
    string mode = parser.retrieve<string>("mode");
    if(mode.empty()){mode = "holdem";}
    
    if(mode != "holdem" && mode != "shortdeck") {
        cerr << "Error: mode " << mode << " not recognized. Use 'holdem' or 'shortdeck'." << endl;
        return 1;
    }

    try {
        if(input_file.empty()) {
            CommandLineTool clt = CommandLineTool(mode,resource_dir);
            clt.startWorking();
        }else{
            cout << "Executing commands from: " << input_file << endl;
            CommandLineTool clt = CommandLineTool(mode,resource_dir);
            clt.execFromFile(input_file);
        }
    } catch (const std::exception& e) {
        cerr << "Runtime Error: " << e.what() << endl;
        return 2;
    }
    return 0;
}
