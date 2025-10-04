#include <iostream>
#include <string>
#include <fstream>

int main() {
    std::ofstream log("c:/Programming/apps/CryoLang/logs/test-stdio.log");
    log << "Test stdio started" << std::endl;
    
    std::string line;
    int count = 0;
    
    while (std::getline(std::cin, line)) {
        count++;
        log << "Line " << count << ": '" << line << "'" << std::endl;
        log.flush();
        
        if (line.find("Content-Length:") == 0) {
            log << "Found Content-Length header!" << std::endl;
        }
        
        if (line.empty()) {
            log << "Empty line - end of headers" << std::endl;
        }
        
        if (count > 20) {
            log << "Stopping after 20 lines to prevent infinite loop" << std::endl;
            break;
        }
    }
    
    log << "Getline returned false. EOF: " << std::cin.eof() << ", fail: " << std::cin.fail() << std::endl;
    log.close();
    return 0;
}