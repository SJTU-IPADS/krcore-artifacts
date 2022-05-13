### configurations ###

USER=
PWD=
PROJECT_PATH=krdmakit
SERVER_GID=fe80:0000:0000:0000:ec0d:9a03:0078:64a6
SERVER_HOST=val12
CLIENT_HOSTS=val14
STR_CLIENT_HOSTS='val14'

USE_PROXY_COMMAND=false # true or false
#USE_PROXY_COMMAND=true # true or false

## Plot related
PLOT=1 # If plot the target figure

### end of configurations ###


STR_ALL_HOSTS='${SERVER_HOST}',${STR_CLIENT_HOSTS}
RUNNER_FLAG=-k=$(USE_PROXY_COMMAND)

HOSTS={'builder':[$(STR_ALL_HOSTS)] , \
'client':[$(STR_CLIENT_HOSTS)], \
'server':['$(SERVER_HOST)'],\
'trigger':['$(SERVER_HOST)'],\
'meta-client':[$(STR_CLIENT_HOSTS)], \
'meta-server':['$(SERVER_HOST)'],\
}
XFactor=${CLIENT_HOSTS} # The length

PLACEHOLDER={'server_gid': '$(SERVER_GID)', \
'port':8888, 'server_host': '$(SERVER_HOST)', 'host_len':10,\
'client_hosts': '$(CLIENT_HOSTS)',\
'run_sec_s': 20, 'run_sec_c': 10, 'meta_server_gid': '$(SERVER_GID)',\
}


template_args="{\
'pwd':'$(PWD)','user':'$(USER)',\
'hosts':$(HOSTS),'path':'$(PROJECT_PATH)',\
'placeholder': $(PLACEHOLDER) } \
"

# Template path list
template-clean=templates-build/template-clean.toml
template-build-dc=templates-build/template-build-dc.toml
template-build-tdc=templates-build/template-build-tdc.toml
template-build-rc=templates-build/template-build-rc.toml
template-build-hybrid=templates-build/template-build-hybrid.toml
template-build-exp=templates-build/template-build-exp.toml
template-build-lite=templates-build/template-build-lite.toml
template-build-meta-rpc=templates-build/template-build-meta-rpc.toml
template-build-meta-client=templates-build/template-build-meta-client.toml
template-build-meta-server=templates-build/template-build-meta-server.toml

# Evaluation runner templates
template-run-verbs-connect=templates-run/control-path/template-verbs-connect.toml
template-run-krcore-connect=templates-run/control-path/template-krcore-connect.toml

template-one-sided-dir=templates-run/data-path/one-sided
template-two-sided-dir=templates-run/data-path/two-sided
template-verbs-race-hashing=templates-run/app/template-verbs-race-hashing.toml
template-krcore-race-hashing=templates-run/app/template-krcore-race-hashing.toml
template-krcore-race-hashing-async=templates-run/app/template-krcore-race-hashing-async.toml
# End of path list

## All test names
data_path_dc_krcore=one-sided-krcore-async-read.dc one-sided-krcore-async-write.dc one-sided-krcore-sync-read.dc one-sided-krcore-sync-write.dc
data_path_rc_krcore=one-sided-krcore-async-read.rc one-sided-krcore-async-write.rc one-sided-krcore-sync-read.rc one-sided-krcore-sync-write.rc
data_path_verbs=one-sided-verbs-async-read.verbs one-sided-verbs-async-write.verbs one-sided-verbs-sync-read.verbs one-sided-verbs-sync-write.verbs

data_path_two_sided_verbs=two-sided-verbs-async.verbs two-sided-verbs-sync.verbs
#data_path_two_sided_krcore=rc-sync.toml
data_path_two_sided_rc=two-sided-rc-async.rc two-sided-rc-sync.rc
data_path_two_sided_dc=two-sided-dc-sync.dc two-sided-dc-async.dc

run_out=out
build_out=run-build.toml

gen-clean:
	python toml_generator.py -f $(template-clean) -o . -d ${template_args}


define builder
	mkdir -p ${run_out}
	python toml_generator.py -f $(1) -o . -d ${template_args}
	python bootstrap.py -f $(build_out) ${RUNNER_FLAG}
endef
define runner
	rm -rf $(2)
	python toml_generator.py -f $(1) -o $(2) -d ${template_args}
	python evaluation_runner.py --input $(2) --arguments="${RUNNER_FLAG}"
	if [ $(PLOT) -eq 1 ];then \
    	python log_analyser.py --input $(2) --xfactor ${XFactor}; \
    fi
endef


$(data_path_dc_krcore): build-dc
# one sided
	@echo $(patsubst %.dc,$(template-one-sided-dir)/template-%.toml,$@)
	$(call runner, $(patsubst %.dc,$(template-one-sided-dir)/template-%.toml,$@), ${run_out}/$@)

$(data_path_rc_krcore): build-rc
# one sided
	@echo $(patsubst %.rc,$(template-one-sided-dir)/template-%.toml,$@)
	$(call runner, $(patsubst %.rc,$(template-one-sided-dir)/template-%.toml,$@), ${run_out}/$@)

$(data_path_verbs): build-cpp
# one sided
	@echo $(patsubst %.verbs,$(template-one-sided-dir)/template-%.toml,$@)
	$(call runner, $(patsubst %.verbs,$(template-one-sided-dir)/template-%.toml,$@), ${run_out}/$@)


$(data_path_two_sided_verbs): build-cpp
	@echo $(patsubst %.verbs,$(template-two-sided-dir)/template-%.toml,$@)
	$(call runner, $(patsubst %.verbs,$(template-two-sided-dir)/template-%.toml,$@), ${run_out}/$@)

$(data_path_two_sided_rc): build-rc
	@echo $(patsubst %.rc,$(template-two-sided-dir)/template-%.toml,$@)
	$(call runner, $(patsubst %.rc,$(template-two-sided-dir)/template-%.toml,$@), ${run_out}/$@)

$(data_path_two_sided_dc): build-tdc
	@echo $(patsubst %.dc,$(template-two-sided-dir)/template-%.toml,$@)
	$(call runner, $(patsubst %.dc,$(template-two-sided-dir)/template-%.toml,$@), ${run_out}/$@)


verbs-connect: build-cpp
	$(call runner, $(template-run-verbs-connect), ${run_out}/$@)

krcore-connect: clean
	$(call builder, $(template-build-meta-server))
	$(call builder, $(template-build-meta-client))
	$(call runner, $(template-run-krcore-connect), ${run_out}/$@)

krcore-connect-rpc: clean
	$(call builder, $(template-build-meta-rpc))
	$(call runner, $(template-run-krcore-connect), ${run_out}/$@)

lite-connect: build-lite
	$(call runner, $(template-run-krcore-connect), ${run_out}/$@)

# Only for self-tests. Run one single template
run-single: build-cpp
	$(call runner, $(template-run-verbs-connect), ${run_out}/$@)


# Built all cpp
build-cpp:
	$(call builder, $(template-build-exp))

build-hybrid: clean
	$(call builder, $(template-build-hybrid))

build-rc: clean
	$(call builder, $(template-build-rc))

build-dc: clean
	$(call builder, $(template-build-dc))

build-tdc:
	$(call builder, $(template-build-tdc))


build-lite: clean
	$(call builder, $(template-build-lite))

race-hashing-krcore: build-hybrid
	$(call runner, $(template-krcore-race-hashing), ${run_out}/$@)

race-hashing-krcore-async: build-hybrid
	$(call runner, $(template-krcore-race-hashing-async), ${run_out}/$@)

race-hashing-verbs: build-cpp
	$(call runner, $(template-verbs-race-hashing), ${run_out}/$@)


### Ploting part
painter=python figures/painter.py
racehashing-painter=python figures/racehasing_analyser.py

fig8a:
	${painter} -i \
	${run_out}/krcore-connect \
	${run_out}/lite-connect \
	${run_out}/verbs-connect \
	-o ${run_out}/$@ --xfactor ${XFactor}

fig9a:
	${painter} -i \
	${run_out}/krcore-connect \
	${run_out}/krcore-connect-rpc \
	-o ${run_out}/$@ --xfactor ${XFactor}

fig10a:
	${painter} -i \
	${run_out}/one-sided-verbs-sync-read.verbs \
	${run_out}/one-sided-krcore-sync-read.rc \
	${run_out}/one-sided-krcore-sync-read.dc \
	-o ${run_out}/$@ --xfactor ${XFactor}

fig10b:
	${painter} -i \
	${run_out}/one-sided-verbs-async-read.verbs \
	${run_out}/one-sided-krcore-async-read.rc \
	${run_out}/one-sided-krcore-async-read.dc \
	-o ${run_out}/$@ --xfactor ${XFactor}

fig10c:
	${painter} -i \
	${run_out}/one-sided-verbs-sync-write.verbs \
	${run_out}/one-sided-krcore-sync-write.rc \
	${run_out}/one-sided-krcore-sync-write.dc \
	-o ${run_out}/$@ --xfactor ${XFactor}

fig10d:
	${painter} -i \
	${run_out}/one-sided-verbs-async-write.verbs \
	${run_out}/one-sided-krcore-async-write.rc \
	${run_out}/one-sided-krcore-async-write.dc \
	-o ${run_out}/$@ --xfactor ${XFactor}


fig11a:
	${painter} -i \
	${run_out}/two-sided-verbs-sync.verbs \
	${run_out}/two-sided-rc-sync.rc \
	${run_out}/two-sided-dc-sync.dc \
	-o ${run_out}/$@ --xfactor ${XFactor}

fig11b:
	${painter} -i \
	${run_out}/two-sided-verbs-async.verbs \
	${run_out}/two-sided-rc-async.rc \
	${run_out}/two-sided-dc-async.dc \
	-o ${run_out}/$@ --xfactor ${XFactor}

fig14:
	${racehashing-painter} -i ${run_out} -o ${run_out}/$@

# Run all tests at once. We do not recommend to do this, since it would take too long...
all-control-path: verbs-connect krcore-connect krcore-connect-rpc lite-connect
all-data-path-one-sided: $(data_path_verbs) $(data_path_rc_krcore) $(data_path_dc_krcore)
all-data-path-two-sided: $(data_path_two_sided_verbs) $(data_path_two_sided_rc) $(data_path_two_sided_dc)
all-race-hashing: race-hashing-verbs race-hashing-krcore race-hashing-krcore-async
all: all-control-path all-data-path-one-sided all-data-path-two-sided all-race-hashing

clean: gen-clean
	pkill python; sleep 0.1
	python bootstrap.py -f $(build_out) ${RUNNER_FLAG}