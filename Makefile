CXX ?= c++
CXXFLAGS := -std=c++17 -Wall -Werror -pedantic -Wextra -Wfatal-errors \
            -Wconversion -Isrc

ifdef RELEASE
CXXFLAGS += -O2 -DNDEBUG -march=native -flto -ffast-math
endif

BUILD := .build
BIN := $(BUILD)/xrun
LIB_OBJS := $(patsubst %.cc,$(BUILD)/%.o,$(wildcard src/*.cc))
OBJS := $(BUILD)/cmd/main.o $(LIB_OBJS)

TEST_BIN := $(BUILD)/test_main
TEST_OBJS := $(BUILD)/cmd/test_main.o $(LIB_OBJS)

DEPS := $(OBJS:.o=.d) $(TEST_OBJS:.o=.d)

.PHONY: compile run clean test

compile: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@

$(TEST_BIN): $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(TEST_OBJS) -o $@

$(BUILD)/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

run: compile
	@$(BIN) $(ARGS)

test: compile $(TEST_BIN)
	@$(TEST_BIN)

clean:
	rm -rf $(BUILD)

fmt:
	~/Workspace/y/tools/scripts/clang_format_all.sh .


-include $(DEPS)
