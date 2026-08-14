/**
 * @file examples/01-actors/09-state-machine.cpp
 * @tier 01-actors
 * @teaches An actor is a natural home for a finite state machine: one thread owns the state, a
 *          transition table owns the rules, and every timed step is a message to self rather
 *          than a wait.
 * @demonstrates qb::Actor, registerEvent<E>, qb::KillEvent, push<E>, getSource(), spawn,
 *               qb::ScopedCoroContext, ctx.sleep, broadcast<KillEvent>, kill(), qb::Main,
 *               addActor<T>, id()
 * @prerequisites 01-actors/06-doing-things-later
 * @expect "Coffee Machine simulation started"
 * @expect "----- Demo Sequence Complete -----"
 * @expect "Broadcasting KillEvent to all actors"
 *
 * @example Actor-Based Finite State Machine (FSM)
 *
 * @brief This example demonstrates how to implement a finite state machine (FSM)
 * using a QB actor. It simulates a coffee vending machine that transitions
 * through various states based on user inputs and internal timed operations.
 *
 * @details
 * The system features two main actors:
 * 1.  `CoffeeMachineActor`:
 *     -   Implements the core FSM logic. Its state (`MachineState`) can be IDLE,
 *         SELECTING, PAYMENT, BREWING, DISPENSING, MAINTENANCE, or ERROR.
 *     -   Manages state transitions triggered by `InputEventMessage`s (e.g., COIN_INSERTED,
 *         BUTTON_PRESSED, CANCEL).
 *     -   Uses a `std::map` as a transition table to define state-specific event handlers.
 *     -   Simulates timed operations like brewing and dispensing by scheduling `DelayedActionMessage`s
 *         to itself with `spawn(...)` + `co_await ctx.sleep(delay)`.
 *     -   Responds to `StatusRequestMessage` with its current operational status.
 *     -   Publishes `StateChangeMessage` to notify subscribers of state transitions.
 * 2.  `UserInterfaceActor`:
 *     -   Simulates user interactions with the coffee machine.
 *     -   Sends `InputEventMessage`s to the `CoffeeMachineActor` to trigger operations.
 *     -   Subscribes to the `CoffeeMachineActor` (via `SubscribeMessage`) to receive
 *         `StateChangeMessage` notifications.
 *     -   Requests and displays machine status by sending `StatusRequestMessage` and
 *         handling `StatusResponseMessage`.
 *     -   Orchestrates a multi-step demo sequence with the same delayed self-messaging.
 *     -   Initiates a system-wide shutdown using `broadcast<qb::KillEvent>()`.
 *
 * This example highlights how actors can encapsulate complex stateful logic and
 * manage timed events effectively.
 *
 * @note DELAYS HERE USED TO BE `qb::io::async::callback([this]() { ... }, delay)`, at SEVENTEEN
 *       sites. That overload is a real timer, but it is not bound to any actor's lifetime: it
 *       captures a raw `this`, and if the actor dies before the timer fires the callback runs
 *       anyway and dereferences freed memory. Nothing here ever killed an actor with a timer
 *       outstanding, so the defect was latent -- the SAME shape in
 *       `example9_trading_system.cpp` is a heap-use-after-free that AddressSanitizer reports on
 *       3 runs out of 3. `spawn(...)` + `co_await ctx.sleep(d)` is the lifetime-bound form and
 *       is what `qb/src/qb/core/Actor.h` tells you to prefer. See `scheduleAction()`.
 *
 * QB Features Demonstrated:
 * - Finite State Machine (FSM) Implementation: Actor (`CoffeeMachineActor`) managing internal states and transitions.
 * - Event-Driven State Changes: Using custom `InputEventMessage`s to drive the FSM.
 * - Asynchronous Timed Operations: `spawn(...)` + `co_await ctx.sleep(delay)` for simulating
 *   processes like brewing, cancelled automatically if the actor is killed.
 * - State Notification/Subscription: `SubscribeMessage` and `StateChangeMessage` for observers.
 * - Status Reporting: `StatusRequestMessage` and `StatusResponseMessage`.
 * - Actor Communication: `push<EventType>(...)`, `event.getSource()`.
 * - System-Wide Shutdown: `broadcast<qb::KillEvent>()`.
 * - Engine and Actor Management: `qb::Main`, `engine.addActor<ActorType>()`, `kill()`.
 */

#include <iomanip>
#include <sstream>
#include <string_view>
#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io.h>
#include <qb/io/async.h>
#include <qb/string.h>
#include <chrono>

using namespace qb;

// Define message types
enum MessageType {
    INPUT_EVENT     = 1, // User input event
    STATE_CHANGE    = 2, // State transition event
    STATUS_REQUEST  = 3, // Request current state
    STATUS_RESPONSE = 4, // Response with current state
    TIMER_EVENT     = 5  // Internal timer event
};

// Possible states for the coffee machine
enum class MachineState {
    IDLE,        // Waiting for user input
    SELECTING,   // User is selecting coffee type
    PAYMENT,     // Waiting for payment
    BREWING,     // Making coffee
    DISPENSING,  // Dispensing coffee
    MAINTENANCE, // Machine is in maintenance mode
    FAULT        // Error state (named FAULT, not ERROR: <windows.h> defines an
                 // object-like `ERROR` macro that would mangle this enumerator)
};

// Convert state to string for display
std::string
stateToString(MachineState state) {
    switch (state) {
        case MachineState::IDLE:
            return "IDLE";
        case MachineState::SELECTING:
            return "SELECTING";
        case MachineState::PAYMENT:
            return "PAYMENT";
        case MachineState::BREWING:
            return "BREWING";
        case MachineState::DISPENSING:
            return "DISPENSING";
        case MachineState::MAINTENANCE:
            return "MAINTENANCE";
        case MachineState::FAULT:
            return "ERROR";
        default:
            return "UNKNOWN";
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
std::string
eventToString(InputEvent event) {
    switch (event) {
        case InputEvent::COIN_INSERTED:
            return "COIN_INSERTED";
        case InputEvent::BUTTON_PRESSED:
            return "BUTTON_PRESSED";
        case InputEvent::CANCEL:
            return "CANCEL";
        case InputEvent::MAINTENANCE_KEY:
            return "MAINTENANCE_KEY";
        case InputEvent::ERROR_DETECTED:
            return "ERROR_DETECTED";
        case InputEvent::RESET:
            return "RESET";
        case InputEvent::BREW_FINISHED:
            return "BREW_FINISHED";
        case InputEvent::DISPENSE_FINISHED:
            return "DISPENSE_FINISHED";
        default:
            return "UNKNOWN";
    }
}

// Coffee types available in the machine
enum class CoffeeType { ESPRESSO, AMERICANO, LATTE, CAPPUCCINO, HOT_WATER };

// Convert coffee type to string for display
std::string
coffeeTypeToString(CoffeeType type) {
    switch (type) {
        case CoffeeType::ESPRESSO:
            return "Espresso";
        case CoffeeType::AMERICANO:
            return "Americano";
        case CoffeeType::LATTE:
            return "Latte";
        case CoffeeType::CAPPUCCINO:
            return "Cappuccino";
        case CoffeeType::HOT_WATER:
            return "Hot Water";
        default:
            return "Unknown";
    }
}

// The FSM's internal input, and the type the transition table is written against.
//
// A PLAIN STRUCT, not the event below. The machine synthesises transitions that never travel
// through a pipe -- BREW_FINISHED and DISPENSE_FINISHED are produced by its own timers -- and the
// previous version built an `InputEventMessage` ON THE STACK for those and passed it straight to
// the handler, bypassing the event system. A `qb::Event` carries a routing header (type id,
// source, destination, bucket size) that only the engine fills in; a stack-constructed one has
// none of it, and any handler that later called `getSource()` or `reply()` on it would read
// uninitialised memory. Keep the event for what crosses actors, and a plain struct for what does
// not.
struct InputCommand {
    InputEvent event;
    CoffeeType coffee_type = CoffeeType::ESPRESSO; // Optional, used for BUTTON_PRESSED
    double     amount      = 0.0;                  // Optional, used for COIN_INSERTED
};

// Input event message
struct InputEventMessage : public Event {
    InputEvent event;
    CoffeeType coffee_type = CoffeeType::ESPRESSO; // Optional, used for BUTTON_PRESSED
    double     amount      = 0.0;                  // Optional, used for COIN_INSERTED

    InputEventMessage(InputEvent ev, CoffeeType type = CoffeeType::ESPRESSO, double amt = 0.0)
        : event(ev)
        , coffee_type(type)
        , amount(amt) {}

    [[nodiscard]] InputCommand
    command() const noexcept {
        return InputCommand{event, coffee_type, amount};
    }
};

// `std::to_string(double)` formats with `%f` -- six decimals -- so a $2.75 cappuccino printed as
// "$2.750000" everywhere money appeared in this file.
std::string
formatMoney(double amount) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << amount;
    return ss.str();
}

// NOTE ON EVENT PAYLOADS: the engine relocates an event with `memcpy` and never runs the source
// destructor, so a payload member may hold no pointer into itself. On libstdc++ a SHORT
// std::string holds exactly that -- `_M_p` addresses its own inline buffer -- so after the
// relocation it still points at the old storage. libc++ recomputes the pointer from `this`, which
// is why the defect is invisible on macOS and corrupts on Linux. This is NOT a cross-core-only
// concern: pipe growth, compaction, `reply()` and `forward()` relocate same-core events too.
// Bounded payloads use `qb::string<N>`; unbounded ones are boxed behind a `std::shared_ptr`.
//
// State change message
struct StateChangeMessage : public Event {
    MachineState   prev_state;
    MachineState   new_state;
    qb::string<96> reason;

    StateChangeMessage(MachineState prev, MachineState next, std::string_view r)
        : prev_state(prev)
        , new_state(next)
        , reason(r) {}
};

// Status request message (empty, just a request for status)
struct StatusRequestMessage : public Event {
    // Empty, just a request for status
};

// Status response message
struct StatusResponseMessage : public Event {
    MachineState   current_state;
    CoffeeType     selected_coffee  = CoffeeType::ESPRESSO;
    double         payment_received = 0.0;
    double         payment_required = 0.0;
    qb::string<96> status_message;

    StatusResponseMessage(MachineState state, CoffeeType coffee, double received, double required, std::string_view message)
        : current_state(state)
        , selected_coffee(coffee)
        , payment_received(received)
        , payment_required(required)
        , status_message(message) {}
};

// Timer event message
struct TimerEventMessage : public Event {
    qb::string<32> timer_id;

    explicit TimerEventMessage(std::string_view id)
        : timer_id(id) {}
};

// Delayed action for simulating timers and handling demo sequence
struct DelayedActionMessage : public Event {
    enum class Action { BREW_COMPLETE, DISPENSE_COMPLETE, CHECK_STATUS, START_DEMO, RUN_DEMO_STEP };

    Action action;
    int    step;

    DelayedActionMessage(Action a, int s = 0)
        : action(a)
        , step(s) {}
};

// Subscription message to register for state changes
struct SubscribeMessage : public Event {
    ActorId subscriber_id;

    explicit SubscribeMessage(ActorId id)
        : subscriber_id(id) {}
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
    using TransitionHandler = std::function<void(const InputCommand &)>;
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

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "CoffeeMachineActor created with ID: " << id() << std::endl;

        // Initialize state
        _current_state    = MachineState::IDLE;
        _selected_coffee  = CoffeeType::ESPRESSO;
        _payment_received = 0.0;
        _payment_required = 0.0;

        // Setup coffee prices
        _coffee_prices[CoffeeType::ESPRESSO]   = 1.50;
        _coffee_prices[CoffeeType::AMERICANO]  = 2.00;
        _coffee_prices[CoffeeType::LATTE]      = 2.50;
        _coffee_prices[CoffeeType::CAPPUCCINO] = 2.75;
        _coffee_prices[CoffeeType::HOT_WATER]  = 0.50;

        // Setup the state transition table
        setupTransitionTable();

        qb::io::cout() << "CoffeeMachineActor started in state: " << stateToString(_current_state) << std::endl;

        co_return true;
    }

    void
    on(InputEventMessage &msg) {
        handleInput(msg.command());
    }

    void
    on(StatusRequestMessage &msg) {
        handleStatusRequest(msg);
    }

    void
    on(DelayedActionMessage &msg) {
        handleDelayedAction(msg);
    }

    void
    on(SubscribeMessage &msg) {
        // Add the subscriber to the list
        _subscribers.push_back(msg.subscriber_id);
        qb::io::cout() << "CoffeeMachineActor: Added subscriber " << msg.subscriber_id << std::endl;
    }

    void
    on(KillEvent &) {
        qb::io::cout() << "CoffeeMachineActor stopped" << std::endl;
        kill();
    }

private:
    // Schedule a self-addressed action after `delay`.
    //
    // THIS IS THE ONE THING TO COPY OUT OF THIS FILE. `spawn` binds the wait to this actor's
    // cancellation scope: kill the actor while the sleep is pending and the coroutine throws
    // `qb::io::async::cancelled_error` and unwinds, touching nothing. The seventeen
    // `qb::io::async::callback([this]() { ... }, delay)` calls this replaced are NOT lifetime
    // bound -- the timer holds a raw `this`, fires after the actor is gone, and reads freed
    // memory. It never fired here only because this demo happens never to kill an actor with a
    // timer outstanding; the identical shape in example9 is a heap-use-after-free that
    // AddressSanitizer reports on 3 runs out of 3. `Actor.h` says it directly: "Prefer spawn()
    // ... it is cancelled automatically when the actor is killed."
    void
    scheduleAction(DelayedActionMessage::Action action, qb::duration delay, int step = 0) const {
        spawn([action, delay, step](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(delay);
            ctx.template push<DelayedActionMessage>(action, step);
        });
    }

    // Set up the state transition table
    void
    setupTransitionTable() {
        // IDLE state transitions
        _transition_table[MachineState::IDLE][InputEvent::BUTTON_PRESSED] = [this](const InputCommand &msg) {
            // Transition to SELECTING when a button is pressed from IDLE
            _selected_coffee  = msg.coffee_type;
            _payment_required = _coffee_prices[_selected_coffee];
            changeState(MachineState::SELECTING, "Coffee type selected: " + coffeeTypeToString(_selected_coffee));
        };

        _transition_table[MachineState::IDLE][InputEvent::MAINTENANCE_KEY] = [this](const InputCommand &msg) {
            // Transition to MAINTENANCE when maintenance key is used
            changeState(MachineState::MAINTENANCE, "Maintenance mode activated");
        };

        _transition_table[MachineState::IDLE][InputEvent::ERROR_DETECTED] = [this](const InputCommand &msg) {
            // Transition to ERROR when an error is detected
            _error_message = "Unknown error detected";
            changeState(MachineState::FAULT, _error_message);
        };

        // SELECTING state transitions
        _transition_table[MachineState::SELECTING][InputEvent::COIN_INSERTED] = [this](const InputCommand &msg) {
            // Transition to PAYMENT when money is inserted
            _payment_received = msg.amount;
            changeState(MachineState::PAYMENT, "Payment received: $" + formatMoney(_payment_received));
        };

        _transition_table[MachineState::SELECTING][InputEvent::CANCEL] = [this](const InputCommand &msg) {
            // Return to IDLE if user cancels
            changeState(MachineState::IDLE, "Selection cancelled");
        };

        _transition_table[MachineState::SELECTING][InputEvent::BUTTON_PRESSED] = [this](const InputCommand &msg) {
            // Change coffee selection
            _selected_coffee  = msg.coffee_type;
            _payment_required = _coffee_prices[_selected_coffee];

            // Stay in SELECTING state but update selection
            qb::io::cout() << "Changed selection to: " << coffeeTypeToString(_selected_coffee) << ", price: $" << _payment_required
                           << std::endl;
        };

        // PAYMENT state transitions
        _transition_table[MachineState::PAYMENT][InputEvent::COIN_INSERTED] = [this](const InputCommand &msg) {
            // Add to payment
            _payment_received += msg.amount;

            qb::io::cout() << "Added $" << msg.amount << ", total payment: $" << _payment_received << " (required: $" << _payment_required
                           << ")" << std::endl;

            // If payment is sufficient, start brewing
            if (_payment_received >= _payment_required) {
                changeState(MachineState::BREWING, "Payment complete, brewing started");

                // Schedule brewing completion with a delayed action after 3 seconds
                scheduleAction(DelayedActionMessage::Action::BREW_COMPLETE, std::chrono::seconds(3));
            }
        };

        _transition_table[MachineState::PAYMENT][InputEvent::CANCEL] = [this](const InputCommand &msg) {
            // Return money and go back to IDLE
            qb::io::cout() << "Returning $" << _payment_received << std::endl;
            _payment_received = 0.0;
            changeState(MachineState::IDLE, "Payment cancelled");
        };

        // BREWING state transitions
        _transition_table[MachineState::BREWING][InputEvent::BREW_FINISHED] = [this](const InputCommand &msg) {
            // Brewing finished, transition to dispensing
            changeState(MachineState::DISPENSING, "Brewing complete, dispensing coffee");

            // Schedule dispensing completion with a delayed action after 2 seconds
            scheduleAction(DelayedActionMessage::Action::DISPENSE_COMPLETE, std::chrono::seconds(2));
        };

        _transition_table[MachineState::BREWING][InputEvent::ERROR_DETECTED] = [this](const InputCommand &msg) {
            // Error during brewing
            _error_message = "Brewing error: Water supply issue";
            changeState(MachineState::FAULT, _error_message);
        };

        // DISPENSING state transitions
        _transition_table[MachineState::DISPENSING][InputEvent::DISPENSE_FINISHED] = [this](const InputCommand &msg) {
            // Reset payment and return to IDLE
            _payment_received = 0.0;
            changeState(MachineState::IDLE, "Coffee dispensed, ready for next order");
        };

        // MAINTENANCE state transitions
        _transition_table[MachineState::MAINTENANCE][InputEvent::RESET] = [this](const InputCommand &msg) {
            // Return to IDLE after maintenance
            changeState(MachineState::IDLE, "Maintenance completed");
        };

        // ERROR state transitions
        _transition_table[MachineState::FAULT][InputEvent::RESET] = [this](const InputCommand &msg) {
            // Reset after error
            _error_message = "";
            changeState(MachineState::IDLE, "Error cleared");
        };

        // Default handler for unhandled state/event combinations
        for (int state = 0; state <= static_cast<int>(MachineState::FAULT); ++state) {
            for (int event = 0; event <= static_cast<int>(InputEvent::DISPENSE_FINISHED); ++event) {
                auto machine_state = static_cast<MachineState>(state);
                auto input_event   = static_cast<InputEvent>(event);

                // Add default handler if no specific handler exists
                if (_transition_table[machine_state].find(input_event) == _transition_table[machine_state].end()) {
                    _transition_table[machine_state][input_event] = [machine_state, input_event](const InputCommand &msg) {
                        // Default is to ignore the event and log it
                        qb::io::cout() << "Ignored event " << eventToString(input_event) << " in state " << stateToString(machine_state)
                                       << std::endl;
                    };
                }
            }
        }
    }

    // Handle input events and perform state transitions
    void
    handleInput(const InputCommand &msg) {
        qb::io::cout() << "Received event: " << eventToString(msg.event) << " in state: " << stateToString(_current_state) << std::endl;

        // Look up the appropriate handler for this state and event
        auto &state_handlers = _transition_table[_current_state];
        auto  it             = state_handlers.find(msg.event);

        if (it != state_handlers.end()) {
            // Execute the transition handler
            it->second(msg);
        } else {
            // This should not happen since we have default handlers
            qb::io::cout() << "Unhandled event " << eventToString(msg.event) << " in state " << stateToString(_current_state) << std::endl;
        }
    }

    // Handle delayed actions (to simulate timers)
    void
    handleDelayedAction(const DelayedActionMessage &msg) {
        switch (msg.action) {
            case DelayedActionMessage::Action::BREW_COMPLETE: {
                // Brewing completed
                qb::io::cout() << "Brewing completed" << std::endl;
                handleInput(InputCommand{InputEvent::BREW_FINISHED});
                break;
            }

            case DelayedActionMessage::Action::DISPENSE_COMPLETE: {
                // Dispensing completed
                qb::io::cout() << "Dispensing completed" << std::endl;
                handleInput(InputCommand{InputEvent::DISPENSE_FINISHED});
                break;
            }

            default:
                break;
        }
    }

    // Handle status request
    void
    handleStatusRequest(const StatusRequestMessage &msg) {
        qb::io::cout() << "Status requested by actor " << msg.getSource() << std::endl;

        // Create status response message
        std::string status_message;

        switch (_current_state) {
            case MachineState::IDLE:
                status_message = "Ready to take orders";
                break;
            case MachineState::SELECTING:
                status_message = "Selected: " + coffeeTypeToString(_selected_coffee) + ", price: $" + formatMoney(_payment_required);
                break;
            case MachineState::PAYMENT:
                status_message = "Payment received: $" + formatMoney(_payment_received) + ", required: $" + formatMoney(_payment_required);
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
            case MachineState::FAULT:
                status_message = "Error: " + _error_message;
                break;
        }

        // Send the response using push
        push<StatusResponseMessage>(msg.getSource(), _current_state, _selected_coffee, _payment_received, _payment_required, status_message);
    }

    // Change state and notify
    void
    changeState(MachineState new_state, const std::string &reason) {
        qb::io::cout() << "State transition: " << stateToString(_current_state) << " -> " << stateToString(new_state) << std::endl;
        qb::io::cout() << "Reason: " << reason << std::endl;

        // Send state change notification to all subscribers
        for (const auto &subscriber_id : _subscribers) {
            push<StateChangeMessage>(subscriber_id, _current_state, new_state, reason);
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

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "UserInterfaceActor created with ID: " << id() << std::endl;
        qb::io::cout() << "UserInterfaceActor started" << std::endl;

        // Subscribe to the coffee machine for state change notifications
        push<SubscribeMessage>(_machine_id, id());

        // Start the demo sequence after a short delay
        scheduleAction(DelayedActionMessage::Action::START_DEMO, std::chrono::seconds(1));

        co_return true;
    }

    void
    on(StatusResponseMessage &msg) {
        handleStatusResponse(msg);
    }

    void
    on(StateChangeMessage &msg) {
        handleStateChange(msg);
    }

    void
    on(DelayedActionMessage &msg) {
        handleDelayedAction(msg);
    }

    void
    on(KillEvent &) {
        qb::io::cout() << "UserInterfaceActor stopped" << std::endl;
        kill();
    }

private:
    // Same helper, same reason, as CoffeeMachineActor::scheduleAction above.
    void
    scheduleAction(DelayedActionMessage::Action action, qb::duration delay, int step = 0) const {
        spawn([action, delay, step](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(delay);
            ctx.template push<DelayedActionMessage>(action, step);
        });
    }

    // Handle status response
    void
    handleStatusResponse(const StatusResponseMessage &msg) {
        qb::io::cout() << "\n=== COFFEE MACHINE STATUS ===" << std::endl;
        qb::io::cout() << "State: " << stateToString(msg.current_state) << std::endl;
        qb::io::cout() << "Selected Coffee: " << coffeeTypeToString(msg.selected_coffee) << std::endl;
        qb::io::cout() << "Payment: $" << msg.payment_received << " / $" << msg.payment_required << std::endl;
        qb::io::cout() << "Message: " << msg.status_message << std::endl;
        qb::io::cout() << "============================\n" << std::endl;
    }

    // Handle state change notification
    void
    handleStateChange(const StateChangeMessage &msg) {
        qb::io::cout() << "Machine state changed: " << stateToString(msg.prev_state) << " -> " << stateToString(msg.new_state) << std::endl;
        qb::io::cout() << "Reason: " << msg.reason << std::endl;
    }

    // Handle delayed actions
    void
    handleDelayedAction(const DelayedActionMessage &msg) {
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
    void
    sendEvent(InputEvent event, CoffeeType coffee_type = CoffeeType::ESPRESSO, double amount = 0.0) {
        push<InputEventMessage>(_machine_id, event, coffee_type, amount);
    }

    // Request status from the coffee machine
    void
    requestStatus() {
        push<StatusRequestMessage>(_machine_id);
    }

    // Run a demonstration sequence
    void
    runDemoSequence() {
        qb::io::cout() << "\n----- Starting Demo Sequence -----" << std::endl;

        // Start with step 0
        scheduleAction(DelayedActionMessage::Action::RUN_DEMO_STEP, std::chrono::milliseconds(100), 0);
    }

    // Run a specific step of the demo
    void
    runDemoStep(int step) {
        switch (step) {
            case 0: {
                // Show initial status
                qb::io::cout() << "\nInitial machine status:" << std::endl;
                requestStatus();

                // Schedule next step
                scheduleAction(DelayedActionMessage::Action::RUN_DEMO_STEP, std::chrono::milliseconds(500), 1);
                break;
            }

            case 1: {
                // Select coffee
                qb::io::cout() << "\n1. Customer selects Cappuccino" << std::endl;
                sendEvent(InputEvent::BUTTON_PRESSED, CoffeeType::CAPPUCCINO);

                // Check status after selection
                scheduleAction(DelayedActionMessage::Action::CHECK_STATUS, std::chrono::milliseconds(500));

                // Schedule next step
                scheduleAction(DelayedActionMessage::Action::RUN_DEMO_STEP, std::chrono::seconds(1), 2);
                break;
            }

            case 2: {
                // Insert partial payment
                qb::io::cout() << "\n2. Customer inserts $1.00" << std::endl;
                sendEvent(InputEvent::COIN_INSERTED, CoffeeType::ESPRESSO, 1.00);

                // Check status after partial payment
                scheduleAction(DelayedActionMessage::Action::CHECK_STATUS, std::chrono::milliseconds(500));

                // Schedule next step
                scheduleAction(DelayedActionMessage::Action::RUN_DEMO_STEP, std::chrono::seconds(1), 3);
                break;
            }

            case 3: {
                // Insert remaining payment
                qb::io::cout() << "\n3. Customer inserts $2.00 more" << std::endl;
                sendEvent(InputEvent::COIN_INSERTED, CoffeeType::ESPRESSO, 2.00);

                // This will trigger brewing automatically

                // Check brewing status after a delay
                scheduleAction(DelayedActionMessage::Action::CHECK_STATUS, std::chrono::milliseconds(500));

                // Check status during brewing
                scheduleAction(DelayedActionMessage::Action::CHECK_STATUS, std::chrono::seconds(2));

                // Schedule next step (after brewing and dispensing should be complete)
                scheduleAction(DelayedActionMessage::Action::RUN_DEMO_STEP, std::chrono::seconds(6), 4);
                break;
            }

            case 4: {
                // Transaction should be complete now
                qb::io::cout() << "\n4. Transaction complete" << std::endl;
                requestStatus();

                // Schedule next step
                scheduleAction(DelayedActionMessage::Action::RUN_DEMO_STEP, std::chrono::seconds(1), 5);
                break;
            }

            case 5: {
                // Try error scenario
                qb::io::cout() << "\n5. Simulating an error condition" << std::endl;
                sendEvent(InputEvent::ERROR_DETECTED);

                // Check status after error
                scheduleAction(DelayedActionMessage::Action::CHECK_STATUS, std::chrono::milliseconds(500));

                // Schedule next step
                scheduleAction(DelayedActionMessage::Action::RUN_DEMO_STEP, std::chrono::seconds(1), 6);
                break;
            }

            case 6: {
                // Reset the machine
                qb::io::cout() << "\n6. Resetting the machine" << std::endl;
                sendEvent(InputEvent::RESET);

                // Final status check
                scheduleAction(DelayedActionMessage::Action::CHECK_STATUS, std::chrono::milliseconds(500));

                // Schedule end of demo
                scheduleAction(DelayedActionMessage::Action::RUN_DEMO_STEP, std::chrono::seconds(1), 7);
                break;
            }

            case 7: {
                // End of demo
                qb::io::cout() << "\n----- Demo Sequence Complete -----\n" << std::endl;

                // Broadcast a kill event to all actors
                qb::io::cout() << "Broadcasting KillEvent to all actors\n" << std::endl;
                broadcast<KillEvent>();

                break;
            }
        }
    }
};

int
main() {
    // Create an actor system
    Main engine;

    // Add the coffee machine actor
    auto machine_id = engine.addActor<CoffeeMachineActor>(0);

    // Add the user interface actor
    engine.addActor<UserInterfaceActor>(0, machine_id);

    // Start the system
    engine.start();

    qb::io::cout() << "Coffee Machine simulation started" << std::endl;

    // Wait for the system to finish
    engine.join();

    return 0;
}