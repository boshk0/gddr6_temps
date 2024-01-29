#include <iostream>
#include <fstream>
#include <string>
#include "httplib.h" // Update the include path if necessary

using namespace httplib;

std::string readMetricsFromFile(const std::string &filename) {
    std::ifstream file(filename);
    std::string content, line;
    while (getline(file, line)) {
        content += line + "\n";
    }
    return content;
}

int main() {
    Server svr;

    // Handler for the root path
    svr.Get("/", [](const Request& req, Response& res) {
        std::string landingPageHtml = "<html>"
                                      "<head><title>Metrics Exporter</title></head>"
                                      "<body>"
                                      "<h1>Welcome to the Metrics Exporter</h1>"
                                      "<p><a href='/metrics'>Go to Metrics</a></p>"
                                      "</body>"
                                      "</html>";
        res.set_content(landingPageHtml, "text/html");
    });

    // Handler for the /metrics path
    svr.Get("/metrics", [](const Request& req, Response& res) {
        std::string metrics = readMetricsFromFile("./metrics.txt");
        res.set_content(metrics, "text/plain");
    });

    // Bind to 0.0.0.0 to make the server accessible from other machines
    std::cout << "Starting metrics server on port 9500..." << std::endl;
    svr.listen("0.0.0.0", 9500);
}
