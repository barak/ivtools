subdirs=box1 box2 box3 box4 box5 button1 button2 button3 bvalue circle drag fonts himom image ips menu patch plot preview print rubband sendplot zoomer
.PHONY: all
all: $(subdirs)
.PHONY: $(subdirs)
$(subdirs):
	$(MAKE) -C $@
.PHONY: clean
clean: $(subdirs:%=%-clean)
.PHONY: $(subdirs:%=%-clean)
$(subdirs:%=%-clean):
	$(MAKE) -C $(@:%-clean=%) clean
