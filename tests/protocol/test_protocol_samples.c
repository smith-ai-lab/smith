// Messages as real implementations write them, read by the generated code: lldb-dap's and an MCP client's shapes. The
// generated tests (test_protocol_dap.c, test_protocol_mcp.c) prove the code agrees with itself; these prove it agrees
// with the protocols: what a schema allows beyond its list of members is kept, and a document that breaks a rule is
// refused where the rule is.
#include "dap.h"
#include "mcp.h"
#include "protocol_support.h"

#include <smith/test.h>

#include <string.h>

static const SmithJsonValue *test_samples_tree(SmithArena *arena, const char *json) {
    return protocol_test_parse(arena, smith_format_string(json));
}

static bool test_samples_has(struct smith_array_const_char_n text, const char *needle) {
    size_t length = strlen(needle);
    for (size_t at = 0; at + length <= text.count[0]; at++) { // bounded by the text
        if (memcmp(text.data + at, needle, length) == 0) return true;
    }
    return false;
}

void test_samples_dap_stopped_event(void) {
    SmithArena arena = smith_test_arena(1 << 22);
    const SmithJsonValue *tree = test_samples_tree(
        &arena, "{\"seq\":5,\"type\":\"event\",\"event\":\"stopped\",\"body\":{\"reason\":\"breakpoint\","
                "\"threadId\":1,\"allThreadsStopped\":true,\"hitBreakpointIds\":[1,2],\"description\":\"hit\"}}");
    SmithDapStoppedEvent event;
    SmithProtocolError error = {};
    protocol_test_accepted(smith_dap_stopped_event_parse(&arena, tree, &event, &error), &error);
    smith_check_equal_i64(event.seq, 5);
    smith_check(event.body.reason.known == SmithDapStoppedEventBodyReasonKnown_Breakpoint);
    smith_check(event.body.has_threadId && event.body.threadId == 1);
    smith_check(event.body.has_allThreadsStopped && event.body.allThreadsStopped);
    smith_check_equal_u64(event.body.hitBreakpointIds.count, 2);
    smith_check_equal_i64(event.body.hitBreakpointIds.items[1], 2);
    smith_check(!event.body.has_text);
}

void test_samples_dap_a_reason_the_schema_does_not_list_is_kept(void) {
    SmithArena arena = smith_test_arena(1 << 22);
    const SmithJsonValue *tree = test_samples_tree(
        &arena, "{\"seq\":6,\"type\":\"event\",\"event\":\"stopped\",\"body\":{\"reason\":\"signal SIGSEGV\"}}");
    SmithDapStoppedEvent event;
    SmithProtocolError error = {};
    protocol_test_accepted(smith_dap_stopped_event_parse(&arena, tree, &event, &error), &error);
    smith_check(event.body.reason.known == SmithDapStoppedEventBodyReasonKnown_Other);
    smith_check(smith_format_is(event.body.reason.text, "signal SIGSEGV"));
    SmithJsonWriter writer = protocol_test_writer(&arena);
    smith_dap_stopped_event_write(&writer, &event);
    struct smith_array_const_char_n text = protocol_test_text(&writer);
    smith_check(test_samples_has(text, "\"reason\":\"signal SIGSEGV\""));
}

void test_samples_dap_launch_arguments_keep_the_adapters_attributes(void) {
    SmithArena arena = smith_test_arena(1 << 22);
    const SmithJsonValue *tree = test_samples_tree(
        &arena, "{\"seq\":2,\"type\":\"request\",\"command\":\"launch\",\"arguments\":{\"program\":\"/tmp/a.out\","
                "\"args\":[\"x\",\"y\"],\"stopOnEntry\":true,\"noDebug\":false}}");
    SmithDapLaunchRequest request;
    SmithProtocolError error = {};
    protocol_test_accepted(smith_dap_launch_request_parse(&arena, tree, &request, &error), &error);
    smith_check(request.arguments.has_noDebug && !request.arguments.noDebug);
    smith_check_equal_u64(request.arguments.additionalProperties.count, 3);
    SmithJsonWriter writer = protocol_test_writer(&arena);
    smith_dap_launch_request_write(&writer, &request);
    struct smith_array_const_char_n text = protocol_test_text(&writer);
    smith_check(test_samples_has(text, "\"program\":\"/tmp/a.out\""));
    smith_check(test_samples_has(text, "\"args\":[\"x\",\"y\"]"));
    smith_check(test_samples_has(text, "\"command\":\"launch\""));
}

void test_samples_dap_initialize_response_with_capabilities(void) {
    SmithArena arena = smith_test_arena(1 << 22);
    const SmithJsonValue *tree = test_samples_tree(
        &arena, "{\"seq\":1,\"type\":\"response\",\"request_seq\":1,\"success\":true,\"command\":\"initialize\","
                "\"body\":{\"supportsConfigurationDoneRequest\":true,\"supportsFunctionBreakpoints\":true,"
                "\"exceptionBreakpointFilters\":[{\"filter\":\"cpp_throw\",\"label\":\"C++ Throw\",\"default\":false}],"
                "\"$__lldb_version\":\"lldb version 23\"}}");
    SmithDapInitializeResponse response;
    SmithProtocolError error = {};
    protocol_test_accepted(smith_dap_initialize_response_parse(&arena, tree, &response, &error), &error);
    smith_check(response.success && response.has_body);
    smith_check(response.body.has_supportsConfigurationDoneRequest && response.body.supportsConfigurationDoneRequest);
    smith_check(response.body.has_exceptionBreakpointFilters);
    smith_check_equal_u64(response.body.exceptionBreakpointFilters.count, 1);
    smith_check(smith_format_is(response.body.exceptionBreakpointFilters.items[0].filter, "cpp_throw"));
    smith_check_equal_u64(response.body.additionalProperties.count, 1); // the adapter's own member
}

void test_samples_dap_a_failure_response_with_a_message_of_its_own(void) {
    SmithArena arena = smith_test_arena(1 << 22);
    const SmithJsonValue *tree = test_samples_tree(
        &arena, "{\"seq\":3,\"type\":\"response\",\"request_seq\":2,\"success\":false,\"command\":\"launch\","
                "\"message\":\"program not found\"}");
    SmithDapLaunchResponse response;
    SmithProtocolError error = {};
    protocol_test_accepted(smith_dap_launch_response_parse(&arena, tree, &response, &error), &error);
    smith_check(!response.success && response.has_message);
    smith_check(response.message.known == SmithDapResponseMessageKnown_Other);
    smith_check(smith_format_is(response.message.text, "program not found"));
}

void test_samples_dap_a_wrong_event_name_and_a_wrong_type_are_refused_where_they_are(void) {
    SmithArena arena = smith_test_arena(1 << 22);
    SmithDapStoppedEvent event;
    SmithProtocolError error = {};
    const SmithJsonValue *tree =
        test_samples_tree(&arena, "{\"seq\":5,\"type\":\"event\",\"event\":\"output\",\"body\":{\"reason\":\"step\"}}");
    smith_check(!smith_dap_stopped_event_parse(&arena, tree, &event, &error));
    smith_check_equal_u64((uint64_t)error.reason, (uint64_t)SmithProtocolReason_BadValue);
    smith_check(error.field != nullptr && strcmp(error.field, "event") == 0);
    error = (SmithProtocolError){};
    tree = test_samples_tree(
        &arena, "{\"seq\":\"five\",\"type\":\"event\",\"event\":\"stopped\",\"body\":{\"reason\":\"step\"}}");
    smith_check(!smith_dap_stopped_event_parse(&arena, tree, &event, &error));
    smith_check_equal_u64((uint64_t)error.reason, (uint64_t)SmithProtocolReason_WrongType);
    smith_check(error.field != nullptr && strcmp(error.field, "seq") == 0);
    smith_check_equal_u64(error.offset, 7); // the byte where "five" begins
    error = (SmithProtocolError){};
    tree = test_samples_tree(&arena, "{\"seq\":5,\"type\":\"event\",\"event\":\"stopped\",\"body\":{}}");
    smith_check(!smith_dap_stopped_event_parse(&arena, tree, &event, &error));
    smith_check_equal_u64((uint64_t)error.reason, (uint64_t)SmithProtocolReason_Missing);
    smith_check(error.type != nullptr && strcmp(error.type, "StoppedEventBody") == 0);
    smith_check(error.field != nullptr && strcmp(error.field, "reason") == 0);
}

void test_samples_mcp_a_tool_call_request_with_either_kind_of_id(void) {
    SmithArena arena = smith_test_arena(1 << 22);
    SmithProtocolError error = {};
    SmithMcpJSONRPCRequest request;
    const SmithJsonValue *tree = test_samples_tree(
        &arena, "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\",\"params\":{\"name\":\"launch\","
                "\"arguments\":{\"program\":\"a.out\"}}}");
    protocol_test_accepted(smith_mcp_jsonrpc_request_parse(&arena, tree, &request, &error), &error);
    smith_check(request.id.kind == SmithMcpRequestIdKind_Integer);
    smith_check_equal_i64(request.id.as.integer, 7);
    smith_check(smith_format_is(request.method, "tools/call"));
    smith_check(request.has_params && request.params->kind == SmithJsonKind_Object);
    tree = test_samples_tree(&arena, "{\"jsonrpc\":\"2.0\",\"id\":\"abc\",\"method\":\"tools/list\"}");
    protocol_test_accepted(smith_mcp_jsonrpc_request_parse(&arena, tree, &request, &error), &error);
    smith_check(request.id.kind == SmithMcpRequestIdKind_String);
    smith_check(smith_format_is(request.id.as.string, "abc"));
    smith_check(!request.has_params);
    tree = test_samples_tree(&arena, "{\"jsonrpc\":\"1.0\",\"id\":1,\"method\":\"x\"}");
    smith_check(!smith_mcp_jsonrpc_request_parse(&arena, tree, &request, &error));
    smith_check(error.field != nullptr && strcmp(error.field, "jsonrpc") == 0);
}

void test_samples_mcp_a_tool_keeps_its_own_input_schema(void) {
    SmithArena arena = smith_test_arena(1 << 22);
    const SmithJsonValue *tree = test_samples_tree(
        &arena,
        "{\"name\":\"breakpoint\",\"description\":\"Set one\",\"inputSchema\":{\"type\":\"object\","
        "\"properties\":{\"file\":{\"type\":\"string\"},\"line\":{\"type\":\"integer\"}},\"required\":[\"file\"]}}");
    SmithMcpTool tool;
    SmithProtocolError error = {};
    protocol_test_accepted(smith_mcp_tool_parse(&arena, tree, &tool, &error), &error);
    smith_check(smith_format_is(tool.name, "breakpoint"));
    smith_check(tool.has_description && smith_format_is(tool.description, "Set one"));
    smith_check_equal_u64(tool.inputSchema.additionalProperties.count, 2); // properties and required
    SmithJsonWriter writer = protocol_test_writer(&arena);
    smith_mcp_tool_write(&writer, &tool);
    struct smith_array_const_char_n text = protocol_test_text(&writer);
    smith_check(
        test_samples_has(text, "\"properties\":{\"file\":{\"type\":\"string\"},\"line\":{\"type\":\"integer\"}}"));
    smith_check(test_samples_has(text, "\"required\":[\"file\"]"));
    smith_check(test_samples_has(text, "\"type\":\"object\""));
}

void test_samples_mcp_a_tool_result_of_text(void) {
    SmithArena arena = smith_test_arena(1 << 22);
    const SmithJsonValue *tree = test_samples_tree(
        &arena, "{\"resultType\":\"complete\",\"content\":[{\"type\":\"text\",\"text\":\"stopped at main\"}],"
                "\"isError\":false}");
    SmithMcpCallToolResult result;
    SmithProtocolError error = {};
    protocol_test_accepted(smith_mcp_call_tool_result_parse(&arena, tree, &result, &error), &error);
    smith_check_equal_u64(result.content.count, 1);
    smith_check(result.content.items[0].kind == SmithMcpContentBlockKind_TextContent);
    smith_check(smith_format_is(result.content.items[0].as.text_content.text, "stopped at main"));
    smith_check(result.has_isError && !result.isError);
    tree = test_samples_tree(&arena, "{\"resultType\":\"complete\",\"content\":[{\"type\":\"video\"}]}");
    smith_check(!smith_mcp_call_tool_result_parse(&arena, tree, &result, &error));
    smith_check_equal_u64((uint64_t)error.reason, (uint64_t)SmithProtocolReason_BadValue);
    smith_check(error.field != nullptr && strcmp(error.field, "type") == 0);
}
