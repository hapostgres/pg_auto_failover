#
# Install CI tools, mostly Citus style checker and linter for C code.
#
# See https://github.com/citusdata/citus/blob/main/STYLEGUIDE.md
#

CITUS_TOOLS = https://github.com/citusdata/tools.git
UNCRUSTIFY = https://github.com/uncrustify/uncrustify/archive/uncrustify-0.68.1.tar.gz
UNCRUSTIFY_DIR = uncrustify-uncrustify-0.68.1

CMAKE_OPTS = -DCMAKE_POLICY_VERSION_MINIMUM=3.5

tools: mkdir uncrustify citus-tools ;

mkdir:
	mkdir tools

uncrustify: mkdir
	curl -L $(UNCRUSTIFY) | tar -C tools -xz
	mkdir tools/$(UNCRUSTIFY_DIR)/build
	cd tools/$(UNCRUSTIFY_DIR)/build && cmake $(CMAKE_OPTS) ..
	make -C tools/$(UNCRUSTIFY_DIR)/build -j5
	sudo make -C tools/$(UNCRUSTIFY_DIR)/build install

citus-tools: mkdir
	git clone --depth 1 $(CITUS_TOOLS) tools/tools
	sudo make -C tools/tools uncrustify/.install
