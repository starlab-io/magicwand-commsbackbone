# STAR LAB PROPRIETARY & CONFIDENTIAL
# Copyright (c) 2017, Star Lab -- All Rights Reserved
# Unauthorized copying of this file, via any medium is strictly prohibited.

TARGETS := \
    report.pdf \
    report.docx

OBJS = report.yaml \
    report-pdf.md \
    report-changes.md \
    overview.md \
    summary_of_work.md \
    architecture.md \
    design.md \
    operation.md \
    artifacts.md \
    development_environment.md \
    future_enhancements.md

.PHONY: all
.DEFAULT_GOAL := all
all:$(TARGETS)

report.pdf: $(OBJS)
	pandoc --latex-engine=xelatex -S -o $@ $^

report.docx: $(OBJS)
	pandoc -S -o $@ $^

.PHONY: clean
clean:
	@-rm -f $(TARGETS)
