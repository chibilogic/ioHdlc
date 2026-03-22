DOXYGEN ?= doxygen
DOXYFILE ?= Doxyfile
DOXYFILE_FULL ?= doc/build/Doxyfile_full
DOXYGEN_OUT ?= build/doxygen
DOT ?= dot
PLANTUML ?= plantuml
DIAGRAM_SRC_DIR ?= doc/diagrams/src
DIAGRAM_SVG_DIR ?= doc/diagrams/svg

DOT_SOURCES := $(wildcard $(DIAGRAM_SRC_DIR)/*.dot)
PUML_SOURCES := $(wildcard $(DIAGRAM_SRC_DIR)/*.puml)
DIAGRAM_SVGS := $(patsubst $(DIAGRAM_SRC_DIR)/%.dot,$(DIAGRAM_SVG_DIR)/%.svg,$(DOT_SOURCES)) \
	$(patsubst $(DIAGRAM_SRC_DIR)/%.puml,$(DIAGRAM_SVG_DIR)/%.svg,$(PUML_SOURCES))

.PHONY: doxygen docs doxygen-full docs-full docs-clean docs-diagrams

docs-diagrams: $(DIAGRAM_SVGS)

$(DIAGRAM_SVG_DIR):
	@mkdir -p "$(DIAGRAM_SVG_DIR)"

$(DIAGRAM_SVG_DIR)/%.svg: $(DIAGRAM_SRC_DIR)/%.dot | $(DIAGRAM_SVG_DIR)
	$(DOT) -Tsvg "$<" -o "$@"

$(DIAGRAM_SVG_DIR)/%.svg: $(DIAGRAM_SRC_DIR)/%.puml | $(DIAGRAM_SVG_DIR)
	$(PLANTUML) -tsvg -pipe < "$<" > "$@"

doxygen docs: docs-diagrams
	@mkdir -p "$(DOXYGEN_OUT)/full"
	$(DOXYGEN) "$(DOXYFILE)"

doxygen-full docs-full: docs-diagrams
	@mkdir -p "$(DOXYGEN_OUT)/full"
	$(DOXYGEN) "$(DOXYFILE_FULL)"

docs-clean:
	rm -rf "$(DOXYGEN_OUT)"