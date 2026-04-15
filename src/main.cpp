#include "application.h"                                    // pull in the Application class declaration so we can instantiate one below

#include <cstdio>                                            // standard C I/O header, gives us std::fprintf for error reporting to stderr

int main() {                                                 // program entry point -- the OS hands control to this function when the executable launches
    Application app;                                         // construct an Application on the stack; RAII means its destructor runs automatically at scope exit
    if (!app.init("Parametric Design Tool", 1600, 900)) {    // try to create the window, set up GL context and ImGui; init() returns false if anything failed
        std::fprintf(stderr, "Application::init failed\n");  // write a human-readable failure message to the standard error stream
        return 1;                                            // non-zero exit code signals failure to the OS / calling process
    }                                                        // end of the init-failure branch
    app.run();                                               // enter the main event/render loop; this call blocks until the user closes the window
    app.shutdown();                                          // tear down ImGui + GLFW cleanly in reverse order of creation
    return 0;                                                // zero exit code signals success to the OS
}                                                            // end of main function
