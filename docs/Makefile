# STAR LAB PROPRIETARY & CONFIDENTIAL
# Copyright (c) 2017, Star Lab -- All Rights Reserved
# Unauthorized copying of this file, via any medium is strictly prohibited.

TARGETS := \
    documentation.pdf \
	executive_summary.pdf

OBJS = report.yaml \
    design.md \
    development_environment.md

.PHONY: all
.DEFAULT_GOAL := all
all:$(TARGETS)

documentation.pdf: $(OBJS)
	pandoc --latex-engine=xelatex -S -o $@ $^
	mv $@ ./artifacts

executive_summary.pdf: executive_summary.md
	pandoc --latex-engine=xelatex -S -o $@ $^
	mv $@ ./artifacts

.PHONY: clean
clean:
	rm -f ./artifacts/documentation.pdf ./artifacts/executive_summary.pdf
