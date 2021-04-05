#ifndef ARDUINOCOMMAND_H_
#define ARDUINOCOMMAND_H_

#include <functional>

#include <WString.h>

struct ArduinoCommand
{
    ArduinoCommand(String const&                                      n,
                   std::function<void()> const&                       ex,
                   std::function<bool(String const& response)> const& rh)
      : name(n)
      , execute(ex)
      , response_handler(rh)
    {
    }

    std::function<void()> execute;
    std::function<void()> response_timeout_handler;

    // Returns true in case expected response/acknowledgment was received
    std::function<bool(String const& response)> response_handler;

    String const  name;
    unsigned long request_start_time{0};  // Start immediately
    unsigned long response_timeout{3000};
    bool          execution_started{false};
};

#endif  // ARDUINOCOMMAND_H_
