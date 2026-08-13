CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Iinclude
SRC := $(wildcard src/*.cpp)
BIN := edit

# --- Tests ---
TEST_DIR := tests
TEST_INC := -I$(TEST_DIR)
TEST_SRC := $(TEST_DIR)/test_main.cpp \
            $(TEST_DIR)/test_document.cpp \
            $(TEST_DIR)/test_cursor.cpp \
$(TEST_DIR)/test_event.cpp \
             $(TEST_DIR)/test_terminal_event.cpp \
             $(TEST_DIR)/test_editor.cpp \
             $(TEST_DIR)/test_selection.cpp \
             $(TEST_DIR)/test_renderer.cpp \
             $(TEST_DIR)/test_modes.cpp \
             $(TEST_DIR)/test_invariants.cpp \
             $(TEST_DIR)/test_utf8.cpp \
             $(TEST_DIR)/test_truncate.cpp \
             $(TEST_DIR)/test_utf8range.cpp \
             $(TEST_DIR)/test_clipboard.cpp \
             $(TEST_DIR)/test_buffers.cpp
TEST_BIN := edit_tests
# Fuentes del programa que se enlazan en los tests (sin main.cpp)
SRC_NO_MAIN := $(filter-out src/main.cpp,$(SRC))

$(BIN): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN)

$(TEST_BIN): $(TEST_SRC) $(SRC_NO_MAIN)
	$(CXX) $(CXXFLAGS) $(TEST_INC) $(TEST_SRC) $(SRC_NO_MAIN) -o $(TEST_BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(BIN) $(TEST_BIN)

.PHONY: test clean