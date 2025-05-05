/**
 * @file example8_state_machine.cpp
 * @brief Example of implementing a finite state machine with qb-core
 * 
 * This example demonstrates how to implement an actor-based state machine
 * using qb-core. The state machine simulates a simple coffee vending machine
 * that transitions between different states based on user inputs and internal
 * processes.
 */

#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io/async.h>
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <chrono>
#include <memory>
#include <functional>
#include <thread>

using namespace qb;

// Define message types
enum MessageType {
    INPUT_EVENT = 1,      // User input event
    STATE_CHANGE = 2,     // State transition event
    STATUS_REQUEST = 3,   // Request current state
    STATUS_RESPONSE = 4,  // Response with current state
    TIMER_EVENT = 5       // Internal timer event
};

// Possible states for the coffee machine
enum class MachineState {
    IDLE,           // Waiting for user input
    SELECTING,      // User is selecting coffee type
    PAYMENT,        // Waiting for payment
    BREWING,        // Making coffee
    DISPENSING,     // Dispensing coffee
    MAINTENANCE,    // Machine is in maintenance mode
    ERROR           // Error state
};

// Convert state to string for display
std::string stateToString(MachineState state) {
    switch (state) {
        case MachineState::IDLE: return "IDLE";
        case MachineState::SELECTING: return "SELECTING";
        case MachineState::PAYMENT: return "PAYMENT";
        case MachineState::BREWING: return "BREWING";
        case MachineState::DISPENSING: return "DISPENSING";
        case MachineState::MAINTENANCE: return "MAINTENANCE";
        case MachineState::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

// Input events that can trigger state transitions
enum class InputEvent {
    COIN_INSERTED,    // User inserted money
    BUTTON_PRESSED,   // User pressed a button
    CANCEL,           // User cancelled operation
    MAINTENANCE_KEY,  // Maintenance key inserted
    ERROR_DETECTED,   // Internal error detected
    RESET,            // Reset button pressed
    BREW_FINISHED,    // Brewing process completed
    DISPENSE_FINISHED // Dispensing completed
};

// Convert event to string for display
std::string eventToString(InputEvent event) {
    switch (event) {
        case InputEvent::COIN_INSERTED: return "COIN_INSERTED";
        case InputEvent::BUTTON_PRESSED: return "BUTTON_PRESSED";
        case InputEvent::CANCEL: return "CANCEL";
        case InputEvent::MAINTENANCE_KEY: return "MAINTENANCE_KEY";
        case InputEvent::ERROR_DETECTED: return "ERROR_DETECTED";
        case InputEvent::RESET: return "RESET";
        case InputEvent::BREW_FINISHED: return "BREW_FINISHED";
        case InputEvent::DISPENSE_FINISHED: return "DISPENSE_FINISHED";
        default: return "UNKNOWN";
    }
}

// Coffee types available in the machine
enum class CoffeeType {
    ESPRESSO,
    AMERICANO,
    LATTE,
    CAPPUCCINO,
    HOT_WATER
};

// Convert coffee type to string for display
std::string coffeeTypeToString(CoffeeType type) {
    switch (type) {
        case CoffeeType::ESPRESSO: return "Espresso";
        case CoffeeType::AMERICANO: return "Americano";
        case CoffeeType::LATTE: return "Latte";
        case CoffeeType::CAPPUCCINO: return "Cappuccino";
        case CoffeeType::HOT_WATER: return "Hot Water";
        default: return "Unknown";
    }
}

// Input event message
struct InputEventMessage : public Event {
    InputEvent event;
    CoffeeType coffee_type = CoffeeType::ESPRESSO; // Optional, used for BUTTON_PRESSED
    double amount = 0.0;                          // Optional, used for COIN_INSERTED
    
    InputEventMessage(InputEvent ev, CoffeeType type = CoffeeType::ESPRESSO, double amt = 0.0)
        : event(ev), coffee_type(type), amount(amt) {}
};

// State change message
struct StateChangeMessage : public Event {
    MachineState prev_state;
    MachineState new_state;
    std::string reason;
    
    StateChangeMessage(MachineState prev, MachineState next, const std::string& r)
        : prev_state(prev), new_state(next), reason(r) {}
};

// Status request message (empty, just a request for status)
struct StatusRequestMessage : public Event {
    // Empty, just a request for status
};

// Status response message
struct StatusResponseMessage : public Event {
    MachineState current_state;
    CoffeeType selected_coffee = CoffeeType::ESPRESSO;
    double payment_received = 0.0;
    double payment_required = 0.0;
    std::string status_message;
    
    StatusResponseMessage(MachineState state, CoffeeType coffee, 
                         double received, double required, 
                         const std::string& message)
        : current_state(state), selected_coffee(coffee),
          payment_received(received), payment_required(required),
          status_message(message) {}
};

// Timer event message
struct TimerEventMessage : public Event {
    std::string timer_id;
    
    explicit TimerEventMessage(const std::string& id) : timer_id(id) {}
};

// Delayed action for simulating timers and handling demo sequence
struct DelayedActionMessage : public Event {
    enum class Action {
        BREW_COMPLETE,
        DISPENSE_COMPLETE,
        CHECK_STATUS,
        START_DEMO,
        RUN_DEMO_STEP
    };
    
    Action action;
    int step;
    
    DelayedActionMessage(Action a, int s = 0)
        : action(a), step(s) {}
};

// Subscription message to register for state changes
struct SubscribeMessage : public Event {
    ActorId subscriber_id;
    
    explicit SubscribeMessage(ActorId id) : subscriber_id(id) {}
};

/**
 * @brief Coffee Machine State Machine Actor
 * 
 * Implements a finite state machine for a coffee vending machine
 * with transitions between states based on input events.
 */
class CoffeeMachineActor : public Actor {
private:
    // Current state of the machine
    MachineState _current_state;
    
    // Current coffee selection
    CoffeeType _selected_coffee;
    
    // Payment information
    double _payment_received;
    double _payment_required;
    
    // Error information
    std::string _error_message;
    
    // Coffee prices
    std::map<CoffeeType, double> _coffee_prices;
    
    // Transition table: maps current state and input event to a handler function
    using TransitionHandler = std::function<void(const InputEventMessage&)>;
    std::map<MachineState, std::map<InputEvent, TransitionHandler>> _transition_table;
    
    // List of subscribers to state changes
    std::vector<ActorId> _subscribers;
    
public:
    CoffeeMachineActor() {
        // Register for events
        registerEvent<InputEventMessage>(*this);
        registerEvent<StatusRequestMessage>(*this);
        registerEvent<DelayedActionMessage>(*this);
        registerEvent<SubscribeMessage>(*this);
        registerEvent<KillEvent>(*this);
    }
    
    bool onInit() override {
        std::cout << "CoffeeMachineActor created with ID: " << id() << std::endl;
        
        // Initialize state
        _current_state = MachineState::IDLE;
        _selected_coffee = CoffeeType::ESPRESSO;
        _payment_received = 0.0;
        _payment_required = 0.0;
        
        // Setup coffee prices
        _coffee_prices[CoffeeType::ESPRESSO] = 1.50;
        _coffee_prices[CoffeeType::AMERICANO] = 2.00;
        _coffee_prices[CoffeeType::LATTE] = 2.50;
        _coffee_prices[CoffeeType::CAPPUCCINO] = 2.75;
        _coffee_prices[CoffeeType::HOT_WATER] = 0.50;
        
        // Setup the state transition table
        setupTransitionTable();
        
        std::cout << "CoffeeMachineActor started in state: " 
                  << stateToString(_current_state) << std::endl;
        
        return true;
    }
    
    void on(InputEventMessage& msg) {
        handleInputEvent(msg);
    }
    
    void on(StatusRequestMessage& msg) {
        handleStatusRequest(msg);
    }
    
    void on(DelayedActionMessage& msg) {
        handleDelayedAction(msg);
    }
    
    void on(SubscribeMessage& msg) {
        // Add the subscriber to the list
        _subscribers.push_back(msg.subscriber_id);
        std::cout << "CoffeeMachineActor: Added subscriber " << msg.subscriber_id << std::endl;
    }
    
    void on(KillEvent&) {
        std::cout << "CoffeeMachineActor stopped" << std::endl;
        kill();
    }
    
private:
    // Set up the state transition table
    void setupTransitionTable() {
        // IDLE state transitions
        _transition_table[MachineState::IDLE][InputEvent::BUTTON_PRESSED] = 
            [this](const InputEventMessage& msg) {
                // Transition to SELECTING when a button is pressed from IDLE
                _selected_coffee = msg.coffee_type;
                _payment_required = _coffee_prices[_selected_coffee];
                changeState(MachineState::SELECTING, "Coffee type selected: " + 
                           coffeeTypeToString(_selected_coffee));
            };
        
        _transition_table[MachineState::IDLE][InputEvent::MAINTENANCE_KEY] = 
            [this](const InputEventMessage& msg) {
                // Transition to MAINTENANCE when maintenance key is used
                changeState(MachineState::MAINTENANCE, "Maintenance mode activated");
            };
        
        _transition_table[MachineState::IDLE][InputEvent::ERROR_DETECTED] = 
            [this](const InputEventMessage& msg) {
                // Transition to ERROR when an error is detected
                _error_message = "Unknown error detected";
                changeState(MachineState::ERROR, _error_message);
            };
        
        // SELECTING state transitions
        _transition_table[MachineState::SELECTING][InputEvent::COIN_INSERTED] = 
            [this](const InputEventMessage& msg) {
                // Transition to PAYMENT when money is inserted
                _payment_received = msg.amount;
                changeState(MachineState::PAYMENT, "Payment received: $" + 
                           std::to_string(_payment_received));
            };
        
        _transition_table[MachineState::SELECTING][InputEvent::CANCEL] = 
            [this](const InputEventMessage& msg) {
                // Return to IDLE if user cancels
                changeState(MachineState::IDLE, "Selection cancelled");
            };
        
        _transition_table[MachineState::SELECTING][InputEvent::BUTTON_PRESSED] = 
            [this](const InputEventMessage& msg) {
                // Change coffee selection
                _selected_coffee = msg.coffee_type;
                _payment_required = _coffee_prices[_selected_coffee];
                
                // Stay in SELECTING state but update selection
                std::cout << "Changed selection to: " << coffeeTypeToString(_selected_coffee)
                          << ", price: $" << _payment_required << std::endl;
            };
        
        // PAYMENT state transitions
        _transition_table[MachineState::PAYMENT][InputEvent::COIN_INSERTED] = 
            [this](const InputEventMessage& msg) {
                // Add to payment
                _payment_received += msg.amount;
                
                std::cout << "Added $" << msg.amount << ", total payment: $" 
                          << _payment_received << " (required: $" << _payment_required << ")" << std::endl;
                
                // If payment is sufficient, start brewing
                if (_payment_received >= _payment_required) {
                    changeState(MachineState::BREWING, "Payment complete, brewing started");
                    
                    // Schedule brewing completion with a delayed action after 3 seconds
                    qb::io::async::callback([this]() {
                        push<DelayedActionMessage>(
                            id(),
                            DelayedActionMessage::Action::BREW_COMPLETE
                        );
                    }, 3.0); // 3 seconds
                }
            };
        
        _transition_table[MachineState::PAYMENT][InputEvent::CANCEL] = 
            [this](const InputEventMessage& msg) {
                // Return money and go back to IDLE
                std::cout << "Returning $" << _payment_received << std::endl;
                _payment_received = 0.0;
                changeState(MachineState::IDLE, "Payment cancelled");
            };
        
        // BREWING state transitions
        _transition_table[MachineState::BREWING][InputEvent::BREW_FINISHED] = 
            [this](const InputEventMessage& msg) {
                // Brewing finished, transition to dispensing
                changeState(MachineState::DISPENSING, "Brewing complete, dispensing coffee");
                
                // Schedule dispensing completion with a delayed action after 2 seconds
                qb::io::async::callback([this]() {
                    push<DelayedActionMessage>(
                        id(),
                        DelayedActionMessage::Action::DISPENSE_COMPLETE
                    );
                }, 2.0); // 2 seconds
            };
        
        _transition_table[MachineState::BREWING][InputEvent::ERROR_DETECTED] = 
            [this](const InputEventMessage& msg) {
                // Error during brewing
                _error_message = "Brewing error: Water supply issue";
                changeState(MachineState::ERROR, _error_message);
            };
        
        // DISPENSING state transitions
        _transition_table[MachineState::DISPENSING][InputEvent::DISPENSE_FINISHED] = 
            [this](const InputEventMessage& msg) {
                // Reset payment and return to IDLE
                _payment_received = 0.0;
                changeState(MachineState::IDLE, "Coffee dispensed, ready for next order");
            };
        
        // MAINTENANCE state transitions
        _transition_table[MachineState::MAINTENANCE][InputEvent::RESET] = 
            [this](const InputEventMessage& msg) {
                // Return to IDLE after maintenance
                changeState(MachineState::IDLE, "Maintenance completed");
            };
        
        // ERROR state transitions
        _transition_table[MachineState::ERROR][InputEvent::RESET] = 
            [this](const InputEventMessage& msg) {
                // Reset after error
                _error_message = "";
                changeState(MachineState::IDLE, "Error cleared");
            };
        
        // Default handler for unhandled state/event combinations
        for (int state = 0; state <= static_cast<int>(MachineState::ERROR); ++state) {
            for (int event = 0; event <= static_cast<int>(InputEvent::DISPENSE_FINISHED); ++event) {
                auto machine_state = static_cast<MachineState>(state);
                auto input_event = static_cast<InputEvent>(event);
                
                // Add default handler if no specific handler exists
                if (_transition_table[machine_state].find(input_event) == 
                    _transition_table[machine_state].end()) {
                    
                    _transition_table[machine_state][input_event] = 
                        [this, machine_state, input_event](const InputEventMessage& msg) {
                            // Default is to ignore the event and log it
                            std::cout << "Ignored event " << eventToString(input_event) 
                                      << " in state " << stateToString(machine_state) << std::endl;
                        };
                }
            }
        }
    }
    
    // Handle input events and perform state transitions
    void handleInputEvent(const InputEventMessage& msg) {
        std::cout << "Received event: " << eventToString(msg.event) 
                  << " in state: " << stateToString(_current_state) << std::endl;
        
        // Look up the appropriate handler for this state and event
        auto& state_handlers = _transition_table[_current_state];
        auto it = state_handlers.find(msg.event);
        
        if (it != state_handlers.end()) {
            // Execute the transition handler
            it->second(msg);
        } else {
            // This should not happen since we have default handlers
            std::cout << "Unhandled event " << eventToString(msg.event) 
                      << " in state " << stateToString(_current_state) << std::endl;
        }
    }
    
    // Handle delayed actions (to simulate timers)
    void handleDelayedAction(const DelayedActionMessage& msg) {
        switch (msg.action) {
            case DelayedActionMessage::Action::BREW_COMPLETE: {
                // Brewing completed
                std::cout << "Brewing completed" << std::endl;
                InputEventMessage brew_finished(InputEvent::BREW_FINISHED);
                handleInputEvent(brew_finished);
                break;
            }
            
            case DelayedActionMessage::Action::DISPENSE_COMPLETE: {
                // Dispensing completed
                std::cout << "Dispensing completed" << std::endl;
                InputEventMessage dispense_finished(InputEvent::DISPENSE_FINISHED);
                handleInputEvent(dispense_finished);
                break;
            }
            
            default:
                break;
        }
    }
    
    // Handle status request
    void handleStatusRequest(const StatusRequestMessage& msg) {
        std::cout << "Status requested by actor " << msg.getSource() << std::endl;
        
        // Create status response message
        std::string status_message;
        
        switch (_current_state) {
            case MachineState::IDLE:
                status_message = "Ready to take orders";
                break;
            case MachineState::SELECTING:
                status_message = "Selected: " + coffeeTypeToString(_selected_coffee) + 
                                ", price: $" + std::to_string(_payment_required);
                break;
            case MachineState::PAYMENT:
                status_message = "Payment received: $" + std::to_string(_payment_received) + 
                                ", required: $" + std::to_string(_payment_required);
                break;
            case MachineState::BREWING:
                status_message = "Brewing " + coffeeTypeToString(_selected_coffee) + "...";
                break;
            case MachineState::DISPENSING:
                status_message = "Dispensing " + coffeeTypeToString(_selected_coffee) + "...";
                break;
            case MachineState::MAINTENANCE:
                status_message = "Machine in maintenance mode";
                break;
            case MachineState::ERROR:
                status_message = "Error: " + _error_message;
                break;
        }
        
        // Send the response using push
        push<StatusResponseMessage>(
            msg.getSource(),
            _current_state,
            _selected_coffee,
            _payment_received,
            _payment_required,
            status_message
        );
    }
    
    // Change state and notify
    void changeState(MachineState new_state, const std::string& reason) {
        std::cout << "State transition: " << stateToString(_current_state) 
                  << " -> " << stateToString(new_state) << std::endl;
        std::cout << "Reason: " << reason << std::endl;
        
        // Send state change notification to all subscribers
        for (const auto& subscriber_id : _subscribers) {
            push<StateChangeMessage>(
                subscriber_id,
                _current_state,
                new_state,
                reason
            );
        }
        
        // Update current state
        _current_state = new_state;
    }
};

/**
 * @brief User Interface Actor
 * 
 * Simulates a user interface that sends commands to the coffee machine
 * and displays the machine's status.
 */
class UserInterfaceActor : public Actor {
private:
    ActorId _machine_id;
    
public:
    UserInterfaceActor(ActorId machine_id) 
        : _machine_id(machine_id) {
        // Register for events
        registerEvent<StatusResponseMessage>(*this);
        registerEvent<StateChangeMessage>(*this);
        registerEvent<DelayedActionMessage>(*this);
        registerEvent<KillEvent>(*this);
    }
    
    bool onInit() override {
        std::cout << "UserInterfaceActor created with ID: " << id() << std::endl;
        std::cout << "UserInterfaceActor started" << std::endl;
        
        // Subscribe to the coffee machine for state change notifications
        push<SubscribeMessage>(_machine_id, id());
        
        // Start the demo sequence after a short delay
        qb::io::async::callback([this]() {
            push<DelayedActionMessage>(
                id(),
                DelayedActionMessage::Action::START_DEMO
            );
        }, 1.0); // 1 second
        
        return true;
    }
    
    void on(StatusResponseMessage& msg) {
        handleStatusResponse(msg);
    }
    
    void on(StateChangeMessage& msg) {
        handleStateChange(msg);
    }
    
    void on(DelayedActionMessage& msg) {
        handleDelayedAction(msg);
    }
    
    void on(KillEvent&) {
        std::cout << "UserInterfaceActor stopped" << std::endl;
        kill();
    }
    
private:
    // Handle status response
    void handleStatusResponse(const StatusResponseMessage& msg) {
        std::cout << "\n=== COFFEE MACHINE STATUS ===" << std::endl;
        std::cout << "State: " << stateToString(msg.current_state) << std::endl;
        std::cout << "Selected Coffee: " << coffeeTypeToString(msg.selected_coffee) << std::endl;
        std::cout << "Payment: $" << msg.payment_received << " / $" << msg.payment_required << std::endl;
        std::cout << "Message: " << msg.status_message << std::endl;
        std::cout << "============================\n" << std::endl;
    }
    
    // Handle state change notification
    void handleStateChange(const StateChangeMessage& msg) {
        std::cout << "Machine state changed: " << stateToString(msg.prev_state)
                  << " -> " << stateToString(msg.new_state) << std::endl;
        std::cout << "Reason: " << msg.reason << std::endl;
    }
    
    // Handle delayed actions
    void handleDelayedAction(const DelayedActionMessage& msg) {
        switch (msg.action) {
            case DelayedActionMessage::Action::START_DEMO:
                runDemoSequence();
                break;
                
            case DelayedActionMessage::Action::RUN_DEMO_STEP:
                runDemoStep(msg.step);
                break;
                
            case DelayedActionMessage::Action::CHECK_STATUS:
                requestStatus();
                break;
                
            default:
                break;
        }
    }
    
    // Send an input event to the coffee machine
    void sendEvent(InputEvent event, CoffeeType coffee_type = CoffeeType::ESPRESSO, double amount = 0.0) {
        push<InputEventMessage>(
            _machine_id,
            event,
            coffee_type,
            amount
        );
    }
    
    // Request status from the coffee machine
    void requestStatus() {
        push<StatusRequestMessage>(_machine_id);
    }
    
    // Run a demonstration sequence
    void runDemoSequence() {
        std::cout << "\n----- Starting Demo Sequence -----" << std::endl;
        
        // Start with step 0
        qb::io::async::callback([this]() {
            push<DelayedActionMessage>(
                id(),
                DelayedActionMessage::Action::RUN_DEMO_STEP,
                0
            );
        }, 0.1); // 100 ms
    }
    
    // Run a specific step of the demo
    void runDemoStep(int step) {
        switch (step) {
            case 0: {
                // Show initial status
                std::cout << "\nInitial machine status:" << std::endl;
                requestStatus();
                
                // Schedule next step
                qb::io::async::callback([this]() {
                    push<DelayedActionMessage>(
                        id(),
                        DelayedActionMessage::Action::RUN_DEMO_STEP,
                        1
                    );
                }, 0.5); // 500 ms
                break;
            }
            
            case 1: {
                // Select coffee
                std::cout << "\n1. Customer selects Cappuccino" << std::endl;
                sendEvent(InputEvent::BUTTON_PRESSED, CoffeeType::CAPPUCCINO);
                
                // Check status after selection
                qb::io::async::callback([this]() {
                    push<DelayedActionMessage>(
                        id(),
                        DelayedActionMessage::Action::CHECK_STATUS
                    );
                }, 0.5); // 500 ms
                
                // Schedule next step
                qb::io::async::callback([this]() {
                    push<DelayedActionMessage>(
                        id(),
                        DelayedActionMessage::Action::RUN_DEMO_STEP,
                        2
                    );
                }, 1.0); // 1 second
                break;
            }
            
            case 2: {
                // Insert partial payment
                std::cout << "\n2. Customer inserts $1.00" << std::endl;
                sendEvent(InputEvent::COIN_INSERTED, CoffeeType::ESPRESSO, 1.00);
                
                // Check status after partial payment
                qb::io::async::callback([this]() {
                    push<DelayedActionMessage>(
                        id(),
                        DelayedActionMessage::Action::CHECK_STATUS
                    );
                }, 0.5); // 500 ms
                
                // Schedule next step
                qb::io::async::callback([this]() {
                    push<DelayedActionMessage>(
                        id(),
                        DelayedActionMessage::Action::RUN_DEMO_STEP,
                        3
                    );
                }, 1.0); // 1 second
                break;
            }
            
            case 3: {
                // Insert remaining payment
                std::cout << "\n3. Customer inserts $2.00 more" << std::endl;
                sendEvent(InputEvent::COIN_INSERTED, CoffeeType::ESPRESSO, 2.00);
                
                // This will trigger brewing automatically
                
                // Check brewing status after a delay
                qb::io::async::callback([this]() {
                    push<DelayedActionMessage>(
                        id(),
                        DelayedActionMessage::Action::CHECK_STATUS
                    );
                }, 0.5); // 500 ms
                
                // Check status during brewing
                qb::io::async::callback([this]() {
                    push<DelayedActionMessage>(
                        id(),
                        DelayedActionMessage::Action::CHECK_STATUS
                    );
                }, 2.0); // 2 seconds
                
                // Schedule next step (after brewing and dispensing should be complete)
                qb::io::async::callback([this]() {
                    push<DelayedActionMessage>(
                        id(),
                        DelayedActionMessage::Action::RUN_DEMO_STEP,
                        4
                    );
                }, 6.0); // 6 seconds
                break;
            }
            
            case 4: {
                // Transaction should be complete now
                std::cout << "\n4. Transaction complete" << std::endl;
                requestStatus();
                
                // Schedule next step
                qb::io::async::callback([this]() {
                    push<DelayedActionMessage>(
                        id(),
                        DelayedActionMessage::Action::RUN_DEMO_STEP,
                        5
                    );
                }, 1.0); // 1 second
                break;
            }
            
            case 5: {
                // Try error scenario
                std::cout << "\n5. Simulating an error condition" << std::endl;
                sendEvent(InputEvent::ERROR_DETECTED);
                
                // Check status after error
                qb::io::async::callback([this]() {
                    push<DelayedActionMessage>(
                        id(),
                        DelayedActionMessage::Action::CHECK_STATUS
                    );
                }, 0.5); // 500 ms
                
                // Schedule next step
                qb::io::async::callback([this]() {
                    push<DelayedActionMessage>(
                        id(),
                        DelayedActionMessage::Action::RUN_DEMO_STEP,
                        6
                    );
                }, 1.0); // 1 second
                break;
            }
            
            case 6: {
                // Reset the machine
                std::cout << "\n6. Resetting the machine" << std::endl;
                sendEvent(InputEvent::RESET);
                
                // Final status check
                qb::io::async::callback([this]() {
                    push<DelayedActionMessage>(
                        id(),
                        DelayedActionMessage::Action::CHECK_STATUS
                    );
                }, 0.5); // 500 ms
                
                // Schedule end of demo
                qb::io::async::callback([this]() {
                    push<DelayedActionMessage>(
                        id(),
                        DelayedActionMessage::Action::RUN_DEMO_STEP,
                        7
                    );
                }, 1.0); // 1 second
                break;
            }
            
            case 7: {
                // End of demo
                std::cout << "\n----- Demo Sequence Complete -----\n" << std::endl;
                
                // Broadcast a kill event to all actors
                std::cout << "Broadcasting KillEvent to all actors\n" << std::endl;
                broadcast<KillEvent>();
                
                break;
            }
        }
    }
};

int main() {
    // Create an actor system
    Main engine;
    
    // Add the coffee machine actor
    auto machine_id = engine.addActor<CoffeeMachineActor>(0);
    
    // Add the user interface actor
    auto ui_id = engine.addActor<UserInterfaceActor>(0, machine_id);
    
    // Start the system
    engine.start();
    
    std::cout << "Coffee Machine simulation started" << std::endl;

    // Wait for the system to finish
    engine.join();
    
    return 0;
} 