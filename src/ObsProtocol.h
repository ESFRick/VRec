#pragma once

#include <chrono>
#include <cstdint>
#include <string>

struct ObsHello {
    int rpcVersion = 1;
    bool requiresAuthentication = false;
    std::string challenge;
    std::string salt;
};

struct ObsRecordingUpdate {
    bool active = false;
    bool hasDuration = false;
    std::chrono::milliseconds duration{0};
};

struct ObsRequestResult {
    std::string requestType;
    std::string requestId;
    bool success = false;
    int code = 0;
    std::string comment;
};

bool ParseObsHello(const std::string& message, ObsHello& hello);
bool IsObsIdentified(const std::string& message);
bool ParseObsOp(const std::string& message, int& op);
std::string BuildObsAuthentication(
    const std::string& password,
    const std::string& salt,
    const std::string& challenge);
std::string BuildObsIdentify(const std::string& password, const ObsHello& hello);
std::string BuildObsRequest(const std::string& requestType, std::uint64_t requestId);
bool ParseObsRecordingUpdate(const std::string& message, ObsRecordingUpdate& update);
bool ParseObsRequestResult(const std::string& message, ObsRequestResult& result);
