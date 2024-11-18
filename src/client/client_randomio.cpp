#include "api.h"
#include "utils.h"
#include <iostream>
#include <random>
#include <spdlog/spdlog.h>
#include <string>
#include <SFML/Network.hpp> // Include SFML for sf::Packet

using namespace cycles;

class Connection {
public:
  GameState receiveGameState() {
    if (!isActive()) {
      throw std::runtime_error("Connection is not active.");
    }
    GameState gameState;
    // Logic to populate gameState
    if (!validateGameState(gameState)) {
      throw std::runtime_error("Received invalid game state.");
    }
    return gameState;
  }

  bool isActive() const {
    // Return true if the connection is active; otherwise, false
    return connectionState == ConnectionState::Active;
  }

  void sendMove(Direction move) {
    if (!isActive()) {
      throw std::runtime_error("Cannot send move; connection is not active.");
    }
    // Logic to send move
    bool sendingFails = false; // Replace with actual sending logic
    if (sendingFails) {
      spdlog::error("Failed to send move: {}", getDirectionValue(move));
      throw std::runtime_error("Move sending failed.");
    }
  }

private:
  enum class ConnectionState { Active, Inactive };
  ConnectionState connectionState = ConnectionState::Inactive;

  bool validateGameState(const GameState& gameState) {
    // Logic to validate game state
    return true;
  }
}; // <-- Missing semicolon added here

class BotClient {
  cycles::Connection connection; // Fully qualify to avoid ambiguity
  std::string name;
  GameState state;
  Player my_player;
  std::mt19937 rng;
  int previousDirection = -1;
  int inertia = 30;

  void onGameStateReceived(sf::Packet& packet) { // Remove const from packet
    try {
      // Extract player positions from the packet and update the game state
      std::map<Id, std::tuple<int, int>> newPositions;
      // Assuming the packet structure is known and you can extract the positions
      sf::Uint32 numPlayers;
      packet >> numPlayers;
      for (sf::Uint32 i = 0; i < numPlayers; ++i) {
        int x, y, r, g, b;
        std::string playerName;
        Id id;
        packet >> x >> y >> r >> g >> b >> playerName >> id;
        newPositions[id] = std::make_tuple(x, y);
      }
      state.updatePlayerPositions(newPositions);
    } catch (const std::exception &e) {
      spdlog::error("Error while receiving game state: {}", e.what());
    }
  }

  bool is_valid_move(Direction direction) {
    try {
      auto new_pos = my_player.position + getDirectionVector(direction);

      // Check if the new position is inside the grid
      if (!state.isInsideGrid(new_pos)) {
        return false;
      }

      // Check if the new position collides with any player position
      auto playerPositions = state.getPlayerPositions();
      for (const auto& pos : playerPositions) {
        if (new_pos == pos) {
          return false; // Collision detected
        }
      }

      // Check if the new position is empty
      if (!state.isCellEmpty(new_pos)) {
        return false;
      }

      return true;
    } catch (const std::exception &e) {
      spdlog::error("Error while validating move: {}", e.what());
      return false;
    }
  }

  Direction decideMove() {
    constexpr int max_attempts = 200;
    int attempts = 0;
    const auto position = my_player.position;
    spdlog::info("Current Position: ({}, {})", my_player.position.x, my_player.position.y);

    const int frameNumber = state.frameNumber;
    float inertialDamping = 1.0;
    auto dist = std::uniform_int_distribution<int>(
        0, 3 + static_cast<int>(inertia * inertialDamping));
    Direction direction;
    do {
      if (attempts >= max_attempts) {
        spdlog::error("{}: Failed to find a valid move after {} attempts", name,
                      max_attempts);
        throw std::runtime_error("Failed to find a valid move after max attempts");
      }
      // Simple random movement
      int proposal = dist(rng);
      if (proposal > 3) {
        proposal = previousDirection;
        inertialDamping =
            0; // Remove inertia if the previous direction is not valid
      }
      direction = getDirectionFromValue(proposal);
      attempts++;
    } while (!is_valid_move(direction));
    spdlog::debug("{}: Valid move found after {} attempts, moving from ({}, "
                  "{}) to ({}, {}) in frame {}",
                  name, position.x, position.y, attempts,
                  position.x + getDirectionVector(direction).x,
                  position.y + getDirectionVector(direction).y, frameNumber);
    return direction;
  }

  void receiveGameState() {
    try {
      state = connection.receiveGameState();
      for (const auto &player : state.players) {
        if (player.name == name) {
          my_player = player;
          break;
        }
      }
    } catch (const std::exception &e) {
      spdlog::error("Error while receiving game state: {}", e.what());
    }
  }

  void sendMove() {
    try {
      spdlog::debug("{}: Sending move", name);
      auto move = decideMove();
      previousDirection = getDirectionValue(move);
      connection.sendMove(move);
    } catch (const std::exception &e) {
      spdlog::error("Error while sending move: {}", e.what());
    }
  }

public:
  BotClient(const std::string &botName) : name(botName) {
    try {
      std::random_device rd;
      rng.seed(rd());
      std::uniform_int_distribution<int> dist(0, 50);
      inertia = dist(rng);
      if (!connection.isActive()) {
        spdlog::critical("{}: Connection is not active", name);
        throw std::runtime_error("Failed to establish an active connection");
      }
    } catch (const std::exception &e) {
      spdlog::critical("Error during BotClient initialization: {}", e.what());
      exit(1);
    }
  }

  void run() {
    while (connection.isActive()) {
      try {
        receiveGameState();
        sendMove();
      } catch (const std::exception &e) {
        spdlog::error("Error during run loop: {}", e.what());
      }
    }
  }
};

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <bot_name>" << std::endl;
    return 1;
  }
#if SPDLOG_ACTIVE_LEVEL == SPDLOG_LEVEL_TRACE
  spdlog::set_level(spdlog::level::debug);
#endif
  try {
    std::string botName = argv[1];
    BotClient bot(botName);
    bot.run();
  } catch (const std::exception &e) {
    spdlog::critical("Unhandled exception in main: {}", e.what());
    return 1;
  }
  return 0;
}
