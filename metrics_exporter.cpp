#include <iostream>
#include <fstream>
#include <string>
#include <exception>
#include "httplib.h" // Update the include path if necessary

using namespace httplib;

// Custom exception for file-related errors
class FileException : public std::exception {
private:
    std::string message;
public:
    FileException(const std::string& msg) : message(msg) {}
    const char* what() const throw () {
        return message.c_str();
    }
};

std::string readMetricsFromFile(const std::string &filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw FileException("Error opening file: " + filename);
    }

    std::string content, line;
    while (getline(file, line)) {
        content += line + "\n";
    }
    file.close();
    return content;
}

int main(int argc, char* argv[]) {
    std::string metricsFilePath = "./metrics.txt"; // Default file path
    if (argc > 1) {
        metricsFilePath = argv[1]; // Override with command-line argument if provided
    }

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

    // Handler for the /metrics path with error handling
    svr.Get("/metrics", [metricsFilePath](const Request& req, Response& res) {
        try {
            std::string metrics = readMetricsFromFile(metricsFilePath);
            res.set_content(metrics, "text/plain");
        } catch (const FileException& e) {
            res.status = 404; // Not Found
            res.set_content(e.what(), "text/plain");
        } catch (const std::exception& e) {
            res.status = 500; // Internal Server Error
            res.set_content("An unexpected error occurred: " + std::string(e.what()), "text/plain");
        }
    });

    // Bind to 0.0.0.0 to make the server accessible from other machines
    std::cout << "Starting metrics server on port 9500..." << std::endl;
    svr.listen("0.0.0.0", 9500);
}
