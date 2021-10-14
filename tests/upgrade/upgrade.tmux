set-option -g default-shell /bin/bash
new-session -s pgautofailover-test-upgrade

send-keys 'make build up' Enter

split-window -v
select-layout even-vertical

send-keys 'sleep 45' Enter
send-keys 'make watch' Enter

split-window -v
select-layout even-vertical

send-keys 'sleep 45' Enter
send-keys 'make version' Enter
send-keys 'sleep 15' Enter
send-keys 'make upgrade-monitor' Enter
send-keys 'make version' Enter
