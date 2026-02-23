
#include "include/httplib.h"
#include <iostream>

int main() {
    httplib::Server svr;
    
    svr.set_websocket_handler("/ws",
        [](const httplib::Request&, httplib::Response&) -> bool {
            return true;
        },
        [](const httplib::Request&, httplib::Response&) {
            
        }
    );
    
    std::cout << "Compilation successful" << std::endl;
    return 0;
}
