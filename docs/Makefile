# STAR LAB PROPRIETARY & CONFIDENTIAL
# Copyright (c) 2017, Star Lab -- All Rights Reserved
# Unauthorized copying of this file, via any medium is strictly prohibited.

TARGETS := \
    report.pdf \
    report.docx

.PHONY: all
.DEFAULT_GOAL := all
all:$(TARGETS)

report.pdf: report/report.yaml report/report-pdf.md report/report-changes.md \
		report/overview.md report/summary_of_work.md report/architecture.md \
		report/design.md report/operation.md report/artifacts.md \
		report/development_environment.md report/future_enhancements.md
	pandoc --latex-engine=xelatex -S -o $@ $^

report.docx: report/report.yaml report/report-pdf.md report/report-changes.md \
		report/overview.md report/summary_of_work.md report/architecture.md \
		report/design.md report/operation.md report/artifacts.md \
		report/development_environment.md report/future_enhancements.md
	pandoc -S -o $@ $^

.PHONY: clean
clean:
	@-rm -f $(TARGETS)
