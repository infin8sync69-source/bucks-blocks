# Deployment kit checks, run by ctest as:
#   cmake -DBUCKS_ROOT=<source dir> -DBUCKS_BIN=<bucks executable> -P deploy_kit.cmake
# Fails if deploy/ drifts from the node or from its own security rules: every node option it
# passes must be one bucks knows, the unit must point at the install layout, the API must stay on
# loopback behind nginx (TLS, Host check, trusted proxy, Secure cookie) and the firewall must never
# open the API port.

cmake_minimum_required(VERSION 3.14)

foreach(var BUCKS_ROOT BUCKS_BIN)
    if(NOT ${var})
        message(FATAL_ERROR "${var} is not set")
    endif()
endforeach()

set(errors "")
set(D "${BUCKS_ROOT}/deploy")

# Options of the shared CLI contract that another change may add after this kit (mining payout and
# threads, word-list directory, Secure cookie); everything else must appear in `bucks --help`.
set(contract_options --mine-address --mine-threads --wordlist-dir --api-secure-cookie)
execute_process(COMMAND "${BUCKS_BIN}" --help OUTPUT_VARIABLE help ERROR_VARIABLE help RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    list(APPEND errors "bucks --help exited with ${rc}")
endif()
function(check_options where text)
    string(REGEX MATCHALL "--[a-z][a-z0-9-]*" opts "${text}")
    list(REMOVE_DUPLICATES opts)
    foreach(o IN LISTS opts)
        string(FIND "${help}" "${o}" in_help)
        list(FIND contract_options "${o}" in_contract)
        if(in_help EQUAL -1 AND in_contract EQUAL -1)
            list(APPEND errors "${where} passes ${o}, which bucks --help does not list")
        endif()
    endforeach()
    set(errors "${errors}" PARENT_SCOPE)
endfunction()

# --- the unit: install layout, explicit UI and word lists, no restart loop on errors a person must fix ---
file(READ "${D}/bucks.service" unit)
string(REPLACE "\\\n" " " unit_joined "${unit}")   # join continuation lines
string(REGEX MATCH "ExecStart=[^\n]*" exec "${unit_joined}")
check_options("bucks.service" "${exec}")
foreach(want "ExecStart=/opt/bucks/current/bin/bucks "
             "--web-root /opt/bucks/current/share/bucks/web"
             "--wordlist-dir /opt/bucks/current/share/bucks/wordlists"
             "--datadir /var/lib/bucks" "User=bucks" "RestartPreventExitStatus=2 3 6 7" "LimitCORE=0")
    string(FIND "${unit}" "${want}" pos)
    if(pos EQUAL -1)
        list(APPEND errors "bucks.service lacks '${want}'")
    endif()
endforeach()

# --- the roles: API on loopback with a token file, nothing the unit already sets ---
foreach(role seed miner console)
    file(STRINGS "${D}/roles/${role}.env" args REGEX "^BUCKS_ARGS=")
    if(NOT args)
        list(APPEND errors "roles/${role}.env has no BUCKS_ARGS")
        continue()
    endif()
    check_options("roles/${role}.env" "${args}")
    foreach(want "--api-bind 127.0.0.1" "--api-token-file /etc/bucks/api-token")
        string(FIND "${args}" "${want}" pos)
        if(pos EQUAL -1)
            list(APPEND errors "roles/${role}.env lacks '${want}'")
        endif()
    endforeach()
    foreach(banned "--api-token " "--network" "--datadir" "--web-root" "--wordlist-dir" "--api-cors-origin")
        string(FIND "${args} " "${banned}" pos)
        if(NOT pos EQUAL -1)
            list(APPEND errors "roles/${role}.env must not pass ${banned} (secret on the command line, or set by the unit)")
        endif()
    endforeach()
    file(STRINGS "${D}/roles/${role}.env" peers REGEX "^BUCKS_PEERS=")
    if(NOT peers)
        list(APPEND errors "roles/${role}.env has no BUCKS_PEERS line")
    endif()
endforeach()
file(STRINGS "${D}/roles/console.env" console_args REGEX "^BUCKS_ARGS=")
foreach(want --api-allowed-host --trust-proxy --api-secure-cookie)
    string(FIND "${console_args}" "${want}" pos)
    if(pos EQUAL -1)
        list(APPEND errors "roles/console.env lacks ${want} (needed behind nginx with TLS)")
    endif()
endforeach()
file(STRINGS "${D}/roles/miner.env" miner_args REGEX "^BUCKS_ARGS=")
foreach(want --auto-mine --mine-address --mine-threads)
    string(FIND "${miner_args}" "${want}" pos)
    if(pos EQUAL -1)
        list(APPEND errors "roles/miner.env lacks ${want}")
    endif()
endforeach()

# --- nginx forwards to the console's API port, as the trusted TLS proxy ---
string(REGEX MATCH "--port ([0-9]+)" m "${console_args}")
set(console_port "${CMAKE_MATCH_1}")
file(READ "${D}/nginx-bucks-console.conf" nginx)
foreach(want "server 127.0.0.1:${console_port};" "proxy_set_header X-Forwarded-Proto https;"
             "proxy_set_header Host $host;" "auth_basic_user_file" "deny all;")
    string(FIND "${nginx}" "${want}" pos)
    if(pos EQUAL -1)
        list(APPEND errors "nginx-bucks-console.conf lacks '${want}'")
    endif()
endforeach()

# --- the firewall opens SSH, P2P and (console) 80/443, never the API port ---
file(STRINGS "${D}/bootstrap-ubuntu.sh" ufw_rules REGEX "^[ \t]*ufw (allow|limit) ")
if(NOT ufw_rules)
    list(APPEND errors "bootstrap-ubuntu.sh opens no ports in ufw (the P2P port must be public)")
endif()
foreach(rule IN LISTS ufw_rules)
    if(NOT rule MATCHES "\"\\$(P2P_PORT|SSH_PORT)/tcp\"|ufw allow (80|443)/tcp")
        list(APPEND errors "bootstrap-ubuntu.sh: unexpected firewall rule: ${rule}")
    endif()
endforeach()

# --- the bootstrap starts the timers, not only enables them (enable alone waits for the next boot) ---
file(READ "${D}/bootstrap-ubuntu.sh" bootstrap)
string(REGEX MATCH "(^|\n)[ \t]*systemctl enable --now[^\n]*" now_line "${bootstrap}")
foreach(timer bucks-backup.timer bucks-healthcheck.timer)
    string(FIND "${now_line}" "${timer}" pos)
    if(pos EQUAL -1)
        list(APPEND errors "bootstrap-ubuntu.sh does not start ${timer} ('systemctl enable --now ${timer}')")
    endif()
endforeach()
# A password on a command line shows in `ps`; openssl reads it from stdin instead.
string(REGEX MATCHALL "openssl passwd[^\n]*" passwd_lines "${bootstrap}")
foreach(line IN LISTS passwd_lines)
    if(NOT line MATCHES "-stdin")
        list(APPEND errors "bootstrap-ubuntu.sh: '${line}' passes the password on the command line (use -stdin)")
    endif()
endforeach()
# A monotonic OnBootSec has long passed when the bootstrap starts the timer: the first check would run at once.
file(READ "${D}/bucks-healthcheck.timer" hc_timer)
if(hc_timer MATCHES "(^|\n)OnBootSec=")
    list(APPEND errors "bucks-healthcheck.timer uses OnBootSec= (use OnActiveSec=: it fires the moment the timer is started)")
endif()

# --- the shell scripts parse ---
find_program(BASH bash)
if(BASH)
    foreach(script deploy/bootstrap-ubuntu.sh deploy/bucks-backup.sh deploy/bucks-healthcheck.sh tools/soak.sh tools/ci.sh)
        execute_process(COMMAND "${BASH}" -n "${BUCKS_ROOT}/${script}" RESULT_VARIABLE rc ERROR_VARIABLE err)
        if(NOT rc EQUAL 0)
            list(APPEND errors "${script} does not parse: ${err}")
        endif()
    endforeach()
endif()

if(errors)
    string(REPLACE ";" "\n  " errors "${errors}")
    message(FATAL_ERROR "Deployment kit check failed:\n  ${errors}")
endif()
message(STATUS "Deployment kit OK")
