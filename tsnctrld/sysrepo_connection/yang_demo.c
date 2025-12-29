#include <libyang/libyang.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysrepo.h>
#include <time.h>
#include <unistd.h>

volatile int exit_application = 0;

static void sigint_handler(int signum) {
    exit_application = 1;
}

/* Helper to print values */
void print_val(const sr_val_t *val) {
    if (!val) return;
    printf("%s = ", val->xpath);
    switch (val->type) {
        case SR_STRING_T:
            printf("'%s'\n", val->data.string_val);
            break;
        case SR_INT32_T:
            printf("%d\n", val->data.int32_val);
            break;
        case SR_UINT32_T:
            printf("%u\n", val->data.uint32_val);
            break;
        case SR_BOOL_T:
            printf("%s\n", val->data.bool_val ? "true" : "false");
            break;
        default:
            printf("(type: %d)\n", val->type);
            break;
    }
}

/*
 * CALLBACK 1: CANDIDATE LOGGER
 * Note: Added 'uint32_t sub_id' argument (Required for Sysrepo v2)
 */
int candidate_log_cb(sr_session_ctx_t *session, uint32_t sub_id, const char *module_name, const char *xpath,
                     sr_event_t event, uint32_t request_id, void *private_data) {
    if (event == SR_EV_DONE) {
        printf("[CANDIDATE] User has modified the candidate store.\n");
    }
    return SR_ERR_OK;
}

/*
 * CALLBACK 2: RUNNING COMMIT HANDLER
 * Note: Added 'uint32_t sub_id' argument
 */
int running_commit_cb(sr_session_ctx_t *session, uint32_t sub_id, const char *module_name, const char *xpath,
                      sr_event_t event, uint32_t request_id, void *private_data) {
    // PHASE 1: VALIDATION (The "Make it so" attempt)
    if (event == SR_EV_CHANGE) {
        printf("[COMMIT] Attempting to commit to RUNNING...\n");

        // Simulate Random Failure (e.g., 30% chance of failure)
        int r = rand() % 100;
        if (r < 30) {
            printf("[COMMIT] [!!!] HARDWARE FAILURE SIMULATED! Aborting commit.\n");
            sr_session_set_error_message(session, "Random hardware failure occurred during apply.");
            return SR_ERR_OPERATION_FAILED;  // NETCONF will return rpc-error
        }

        printf("[COMMIT] Hardware apply successful (Simulation).\n");
    }

    // PHASE 2: FINALIZATION
    else if (event == SR_EV_DONE) {
        printf("[COMMIT] Change successfully committed to DB.\n");
    }

    return SR_ERR_OK;
}

/*
 * CALLBACK 3: OPERATIONAL DATA PROVIDER
 * Note: Added 'uint32_t sub_id' argument
 */
int oper_data_cb(sr_session_ctx_t *session, uint32_t sub_id, const char *module_name, const char *path,
                 const char *request_xpath, uint32_t request_id, struct lyd_node **parent, void *private_data) {
    printf("[OPER-DATA] Request received for: %s\n", path);

    // Get current time string
    time_t now = time(NULL);
    char *time_str = ctime(&now);
    time_str[strlen(time_str) - 1] = '\0';  // Remove newline

    // Get the libyang context
    const struct ly_ctx *ctx = sr_session_acquire_context(session);

    // Create the data node.
    lyd_new_path(*parent, ctx, "/example-demo:data/current-time", time_str, 0, parent);

    sr_session_release_context(session);
    return SR_ERR_OK;
}

int main(int argc, char **argv) {
    sr_conn_ctx_t *connection = NULL;
    sr_session_ctx_t *session = NULL;
    sr_subscription_ctx_t *sub = NULL;
    int rc = SR_ERR_OK;

    srand(time(NULL));

    // 1. Connect
    printf("Connecting to Sysrepo...\n");
    if ((rc = sr_connect(SR_CONN_DEFAULT, &connection)) != SR_ERR_OK) {
        fprintf(stderr, "ERROR: sr_connect failed: %s\n", sr_strerror(rc));
        goto cleanup;
    }

    // 2. Start session
    printf("Starting session...\n");
    if ((rc = sr_session_start(connection, SR_DS_RUNNING, &session)) != SR_ERR_OK) {
        fprintf(stderr, "ERROR: sr_session_start failed: %s\n", sr_strerror(rc));
        goto cleanup;
    }

    // 3. Subscribe to RUNNING
    printf("Subscribing to 'example-demo' (Running)...\n");
    rc = sr_module_change_subscribe(session, "example-demo", NULL, running_commit_cb, NULL, 0, SR_SUBSCR_DEFAULT, &sub);
    if (rc != SR_ERR_OK) {
        fprintf(stderr, "ERROR: Subscribe (Running) failed: %s\n", sr_strerror(rc));
        goto cleanup;
    }

    // 4. Subscribe to CANDIDATE
    printf("Subscribing to 'example-demo' (Candidate)...\n");
    sr_session_switch_ds(session, SR_DS_CANDIDATE);
    rc = sr_module_change_subscribe(session, "example-demo", NULL, candidate_log_cb, NULL, 0, SR_SUBSCR_DEFAULT, &sub);
    sr_session_switch_ds(session, SR_DS_RUNNING);  // Switch back
    if (rc != SR_ERR_OK) {
        fprintf(stderr, "ERROR: Subscribe (Candidate) failed: %s\n", sr_strerror(rc));
        goto cleanup;
    }

    // 5. Subscribe to OPERATIONAL
    printf("Subscribing to 'example-demo' (Operational)...\n");
    rc = sr_oper_get_subscribe(session, "example-demo", "/example-demo:data/current-time", oper_data_cb, NULL,
                               SR_SUBSCR_DEFAULT, &sub);

    if (rc != SR_ERR_OK) {
        fprintf(stderr, "ERROR: Subscribe (Operational) failed: %s\n", sr_strerror(rc));
        goto cleanup;
    }

    printf("App running successfully. Ctrl+C to stop.\n");

    signal(SIGINT, sigint_handler);
    while (!exit_application) sleep(1);

cleanup:
    if (sub) sr_unsubscribe(sub);
    if (session) sr_session_stop(session);
    if (connection) sr_disconnect(connection);
    return rc;
}