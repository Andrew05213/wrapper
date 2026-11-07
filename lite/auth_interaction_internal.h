#pragma once

#include "auth_interaction.h"

enum class AuthInteractionResult {
    NotHandled, Answered, Cancelled, Unsupported, TimedOut, Unavailable, HandlerFailed
};

uint64_t auth_interaction_begin_attempt(uint64_t downstream_attempt_id);
AuthInteractionResult auth_interaction_end_attempt(uint64_t attempt_id);
bool auth_interaction_handles_attempt();
uint64_t auth_interaction_item_id();
AuthInteractionResult auth_interaction_ask(AuthInteractionRequest request, AuthInteractionResponse& answer);
bool auth_interaction_utf8(const std::string& text);
bool auth_interaction_get_credentials(std::string& user, std::string& password);
void auth_interaction_set_credentials(const std::string& user, const std::string& password);
size_t auth_interaction_worker_count();
void auth_interaction_retire(AuthInteractionResult result);
AuthInteractionResult auth_interaction_status();
