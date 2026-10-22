LOG = evaluation.log
LEAKAGE_DAT = leakage.csv
PERFORMANCE_DAT = performance.csv
LEAKAGE_TABLE_DAT = leakage_matrix.csv
LEAKAGE_TABLE_SRC = leakage_table.tex
LEAKAGE_TABLE_PDF = $(LEAKAGE_TABLE_SRC:.tex=.pdf)

# Sources the containerised evaluation is built from. A change to any of them
# invalidates the captured log, so ${LOG} -- and with it both CSVs -- has to be
# re-recorded rather than silently re-parsed from a stale capture.
MAIN_REPO = TME-MK-Fine-Grained-Encryption-Integrity
EVAL_SRC = $(MAIN_REPO)/Makefile \
           $(wildcard $(MAIN_REPO)/*.c) \
           $(wildcard $(MAIN_REPO)/*.h) \
           $(wildcard $(MAIN_REPO)/*.asm) \
           $(wildcard $(MAIN_REPO)/patches/*.patch)

# The evaluation only yields valid data on the patched TME-Box kernel: on any
# other kernel every protected scenario aborts in the partition_alloc tagging
# check ("tagging.cc(271) Check failed: fallback == 0") and the capture holds no
# results at all. Set FORCE_EVAL=1 to bypass the check.
EVAL_KERNEL ?= 5.15.0-tmebox

all: plot_table

# Capture into $@.tmp and only publish it once it actually contains results, so
# that a run on the wrong kernel -- or one that dies part-way -- cannot replace
# a good ${LOG} (and with it both CSVs) with a worthless one.
${LOG}: build.sh Dockerfile docker-compose.yml .env ${EVAL_SRC}
	@if [ -z "${FORCE_EVAL}" ]; then \
	  case "$$(uname -r)" in \
	    ${EVAL_KERNEL}*) ;; \
	    *) echo "ERROR: running kernel '$$(uname -r)', need '${EVAL_KERNEL}*'." >&2; \
	       echo "       Reboot into the TME-Box kernel, or set FORCE_EVAL=1." >&2; \
	       echo "       Refusing to overwrite $@." >&2; \
	       exit 1 ;; \
	  esac; \
	fi
	docker compose up --build 2>&1 | tee $@.tmp
	@if ! grep -q 'SCORE:' $@.tmp || ! grep -q 'CSV:TME-Box' $@.tmp; then \
	  echo "ERROR: capture has no leakage scores and/or no benchmark rows." >&2; \
	  echo "       Keeping the previous $@; failed capture left in $@.tmp." >&2; \
	  exit 1; \
	fi
	mv $@.tmp $@

${LEAKAGE_DAT}: parse_evaluation.py ${LOG}
	python3 $< --leakage-csv ${LEAKAGE_DAT} --performance-csv ${PERFORMANCE_DAT} ${LOG}
${PERFORMANCE_DAT}: parse_evaluation.py ${LOG}
	python3 $< --leakage-csv ${LEAKAGE_DAT} --performance-csv ${PERFORMANCE_DAT} ${LOG}

${LEAKAGE_TABLE_DAT}: make_leakage_matrix.py ${LEAKAGE_DAT}
	python3 $< -o $@ ${LEAKAGE_DAT}
${LEAKAGE_TABLE_PDF}: ${LEAKAGE_TABLE_SRC} ${LEAKAGE_TABLE_DAT}
	docker run --rm -v ${PWD}:/workdir -w /workdir texlive/texlive:latest-full latexmk -pdf $<


evaluate: ${LOG}
parse: ${LEAKAGE_DAT} ${PERFORMANCE_DAT}
plot_table: ${LEAKAGE_TABLE_PDF}

clean: clean_tex clean_docker
clean_docker:
	docker compose down -v
clean_tex:
	rm -f $(LEAKAGE_TABLE_SRC:.tex=.aux) \
		  $(LEAKAGE_TABLE_SRC:.tex=.log) \
		  $(LEAKAGE_TABLE_SRC:.tex=.fdb_latexmk) \
		  $(LEAKAGE_TABLE_SRC:.tex=.fls) \
		  build.out
clean_all: clean
	rm -f ${LOG} \
		  ${LOG}.tmp \
		  ${LEAKAGE_DAT} \
		  ${PERFORMANCE_DAT} \
		  ${LEAKAGE_TABLE_DAT} \
		  ${LEAKAGE_TABLE_PDF}
