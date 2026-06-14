CXX ?= c++
CXXFLAGS := -std=c++17 -Wall -Werror -pedantic -Wextra -Wfatal-errors \
            -Wconversion -Isrc

ifdef RELEASE
CXXFLAGS += -O2 -DNDEBUG -march=native -flto -ffast-math
endif

BUILD := .build
BIN := $(BUILD)/xrun
SRCS := cmd/main.cc $(wildcard src/*.cc)
OBJS := $(patsubst %.cc,$(BUILD)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: compile run clean test

compile: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@

$(BUILD)/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

run: compile
	@$(BIN) $(ARGS)

test: compile
	@./test.sh

clean:
	rm -rf $(BUILD)

-include $(DEPS)
