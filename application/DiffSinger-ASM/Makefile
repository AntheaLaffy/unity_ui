.PHONY: all run demo check render-check window-check engine sanitize visual-check
all:
	$(MAKE) -C ../.. diffsinger-build
run:
	$(MAKE) -C ../.. diffsinger DIFFSINGER_ARGS='$(ARGS)'
demo:
	$(MAKE) -C ../.. diffsinger-demo
check:
	$(MAKE) -C ../.. diffsinger-check
render-check:
	$(MAKE) -C ../.. diffsinger-render-check
window-check:
	$(MAKE) -C ../.. diffsinger-window-check
engine:
	$(MAKE) -C ../.. diffsinger-engine
sanitize:
	$(MAKE) -C ../.. diffsinger-sanitize
visual-check:
	$(MAKE) -C ../.. diffsinger-visual-check

.PHONY: ustx-check ustx-window-check
ustx-check:
	$(MAKE) -C ../.. diffsinger-ustx-check
ustx-window-check:
	$(MAKE) -C ../.. diffsinger-ustx-window-check

.PHONY: audio-check audio-window-check
audio-check:
	$(MAKE) -C ../.. diffsinger-audio-check
audio-window-check:
	$(MAKE) -C ../.. diffsinger-audio-window-check

.PHONY: frontend-check frontend-window-check
frontend-check:
	$(MAKE) -C ../.. diffsinger-frontend-check
frontend-window-check:
	$(MAKE) -C ../.. diffsinger-frontend-window-check
