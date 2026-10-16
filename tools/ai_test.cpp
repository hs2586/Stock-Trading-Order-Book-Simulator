// ai_test.cpp - checks that the AI client works, before we plug it into the analyzer
#include <iostream>
#include "ai_client.hpp"

int main() {
    std::cout << "Provider: " << ai::providerName() << "\nAsking... (the first answer can take a while)\n";
    std::string error;
    const std::string answer = ai::ask("In one sentence, what is a buffer overflow?", error, 200);
    if (answer.empty()) {
        std::cerr << "AI error: " << error << "\n";
        return 1;
    }
    std::cout << "\n" << answer << "\n";
    return 0;
}