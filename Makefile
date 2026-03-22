PI      = john@calendar.local
PI_DIR  = ~/eink-calendar
SERVICE = eink-calendar

.PHONY: update install

# Pull latest code and restart the daemon
update:
	ssh $(PI) "cd $(PI_DIR) && git pull && sudo systemctl restart $(SERVICE)"

# First-time setup: clone repo, install service, enable on boot
install:
	ssh $(PI) "git clone https://github.com/joram/eink-calendar.git $(PI_DIR) || (cd $(PI_DIR) && git pull)"
	ssh $(PI) "cd $(PI_DIR) && bash setup.sh"
	scp eink-calendar.service $(PI):/tmp/eink-calendar.service
	ssh $(PI) "sudo mv /tmp/eink-calendar.service /etc/systemd/system/$(SERVICE).service \
		&& sudo systemctl daemon-reload \
		&& sudo systemctl enable $(SERVICE) \
		&& sudo systemctl start $(SERVICE)"
