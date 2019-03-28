# STAR LAB PROPRIETARY & CONFIDENTIAL
# Copyright (c) 2017, Star Lab -- All Rights Reserved
# Unauthorized copying of this file, via any medium is strictly prohibited.

TARGETS := \
    UsersGuide.pdf \
	ExecutiveSummary.pdf \
	DevelopersGuide.pdf

OBJS = report.yaml \
    development_environment.md \
    design.md


.PHONY: all
.DEFAULT_GOAL := all
all:$(TARGETS)

define build_pdf
pandoc --latex-engine=xelatex \
	--toc --toc-depth=2 \
	-V geometry:margin=3cm \
	-S -o $@ $^
endef

DevelopersGuide.pdf: src/developers_guide.md
	@$(build_pdf)

UsersGuide.pdf: src/users_guide.md
	@$(build_pdf)

ExecutiveSummary.pdf: src/executive_summary.md
	@$(build_pdf)

.PHONY: clean
clean:
	rm -f $(TARGETS)
