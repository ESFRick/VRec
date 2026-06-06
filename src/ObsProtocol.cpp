#include "ObsProtocol.h"

#include <Windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <nlohmann/json.hpp>

#include <array>
#include <stdexcept>
#include <vector>

namespace {

using nlohmann::json;

std::vector<unsigned char> Sha256(const std::string& input)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectSize = 0;
    DWORD hashSize = 0;
    DWORD resultSize = 0;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0
        || BCryptGetProperty(
               algorithm,
               BCRYPT_OBJECT_LENGTH,
               reinterpret_cast<PUCHAR>(&objectSize),
               sizeof(objectSize),
               &resultSize,
               0) < 0
        || BCryptGetProperty(
               algorithm,
               BCRYPT_HASH_LENGTH,
               reinterpret_cast<PUCHAR>(&hashSize),
               sizeof(hashSize),
               &resultSize,
               0) < 0) {
        if (algorithm) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
        throw std::runtime_error("Could not initialize SHA-256");
    }

    std::vector<unsigned char> object(objectSize);
    std::vector<unsigned char> digest(hashSize);
    const NTSTATUS createStatus = BCryptCreateHash(
        algorithm,
        &hash,
        object.data(),
        static_cast<ULONG>(object.size()),
        nullptr,
        0,
        0);
    const NTSTATUS dataStatus = createStatus < 0
        ? createStatus
        : BCryptHashData(
              hash,
              reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
              static_cast<ULONG>(input.size()),
              0);
    const NTSTATUS finishStatus = dataStatus < 0
        ? dataStatus
        : BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);

    if (hash) {
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);

    if (finishStatus < 0) {
        throw std::runtime_error("Could not calculate SHA-256");
    }
    return digest;
}

std::string Base64(const std::vector<unsigned char>& value)
{
    DWORD size = 0;
    const DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
    if (!CryptBinaryToStringA(value.data(), static_cast<DWORD>(value.size()), flags, nullptr, &size)) {
        throw std::runtime_error("Could not size Base64 output");
    }

    std::string encoded(size, '\0');
    if (!CryptBinaryToStringA(value.data(), static_cast<DWORD>(value.size()), flags, encoded.data(), &size)) {
        throw std::runtime_error("Could not encode Base64 output");
    }
    encoded.resize(size);
    return encoded;
}

const json* ObjectMember(const json& object, const char* key)
{
    if (!object.is_object()) {
        return nullptr;
    }
    const auto member = object.find(key);
    return member != object.end() && member->is_object() ? &*member : nullptr;
}

} // namespace

bool ParseObsHello(const std::string& message, ObsHello& hello)
{
    try {
        const json root = json::parse(message);
        if (!root.is_object() || root.value("op", -1) != 0) {
            return false;
        }

        const json* data = ObjectMember(root, "d");
        if (!data) {
            return false;
        }

        ObsHello parsed;
        parsed.rpcVersion = data->value("rpcVersion", 1);
        if (const json* authentication = ObjectMember(*data, "authentication")) {
            parsed.challenge = authentication->value("challenge", std::string());
            parsed.salt = authentication->value("salt", std::string());
            if (parsed.challenge.empty() || parsed.salt.empty()) {
                return false;
            }
            parsed.requiresAuthentication = true;
        }
        hello = std::move(parsed);
        return true;
    } catch (const json::exception&) {
        return false;
    }
}

bool IsObsIdentified(const std::string& message)
{
    try {
        const json root = json::parse(message);
        return root.is_object() && root.value("op", -1) == 2;
    } catch (const json::exception&) {
        return false;
    }
}

bool ParseObsOp(const std::string& message, int& op)
{
    try {
        const json root = json::parse(message);
        if (!root.is_object()) {
            return false;
        }
        const auto value = root.find("op");
        if (value == root.end() || !value->is_number_integer()) {
            return false;
        }
        op = value->get<int>();
        return true;
    } catch (const json::exception&) {
        return false;
    }
}

std::string BuildObsAuthentication(
    const std::string& password,
    const std::string& salt,
    const std::string& challenge)
{
    const std::string secret = Base64(Sha256(password + salt));
    return Base64(Sha256(secret + challenge));
}

std::string BuildObsIdentify(const std::string& password, const ObsHello& hello)
{
    json data = {
        { "rpcVersion", hello.rpcVersion },
        { "eventSubscriptions", 0x7FF },
    };
    if (hello.requiresAuthentication) {
        data["authentication"] = BuildObsAuthentication(password, hello.salt, hello.challenge);
    }
    return json({ { "op", 1 }, { "d", std::move(data) } }).dump();
}

std::string BuildObsRequest(const std::string& requestType, std::uint64_t requestId)
{
    return json({
        { "op", 6 },
        { "d", {
            { "requestType", requestType },
            { "requestId", std::to_string(requestId) },
        } },
    }).dump();
}

bool ParseObsRecordingUpdate(const std::string& message, ObsRecordingUpdate& update)
{
    try {
        const json root = json::parse(message);
        const int op = root.value("op", -1);
        const json* data = ObjectMember(root, "d");
        if (!data) {
            return false;
        }

        const json* state = nullptr;
        if (op == 5 && data->value("eventType", std::string()) == "RecordStateChanged") {
            state = ObjectMember(*data, "eventData");
        } else if (op == 7 && data->value("requestType", std::string()) == "GetRecordStatus") {
            const json* requestStatus = ObjectMember(*data, "requestStatus");
            if (!requestStatus || !requestStatus->value("result", false)) {
                return false;
            }
            state = ObjectMember(*data, "responseData");
        }
        if (!state || !state->contains("outputActive") || !(*state)["outputActive"].is_boolean()) {
            return false;
        }

        ObsRecordingUpdate parsed;
        parsed.active = (*state)["outputActive"].get<bool>();
        const auto duration = state->find("outputDuration");
        if (duration != state->end() && duration->is_number_integer()) {
            parsed.hasDuration = true;
            parsed.duration = std::chrono::milliseconds(duration->get<std::int64_t>());
        }
        update = parsed;
        return true;
    } catch (const json::exception&) {
        return false;
    }
}

bool ParseObsRequestResult(const std::string& message, ObsRequestResult& result)
{
    try {
        const json root = json::parse(message);
        if (!root.is_object() || root.value("op", -1) != 7) {
            return false;
        }
        const json* data = ObjectMember(root, "d");
        const json* status = data ? ObjectMember(*data, "requestStatus") : nullptr;
        if (!data || !status) {
            return false;
        }

        ObsRequestResult parsed;
        parsed.requestType = data->value("requestType", std::string());
        parsed.requestId = data->value("requestId", std::string());
        parsed.success = status->value("result", false);
        parsed.code = status->value("code", 0);
        parsed.comment = status->value("comment", std::string());
        if (parsed.requestType.empty() || parsed.requestId.empty()) {
            return false;
        }
        result = std::move(parsed);
        return true;
    } catch (const json::exception&) {
        return false;
    }
}
