CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Iinclude -MMD -MP
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
             $(TEST_DIR)/test_buffers.cpp \
             $(TEST_DIR)/test_filebrowser.cpp
TEST_BIN := edit_tests
# Fuentes del programa que se enlazan en los tests (sin main.cpp)
SRC_NO_MAIN := $(filter-out src/main.cpp,$(SRC))

# --- Compilacion por objetos con dependencias automaticas (-MMD -MP) ----
# Cada .cpp se compila a su .o (regla %%.o) y se linkea aparte. Los .d
# generados por -MMD guardan las deps de headers: tocar un header solo
# recompila los .o que lo incluyen, y tocar un test solo recompila ese .o.
OBJ := $(SRC:.cpp=.o)
TEST_OBJ := $(TEST_SRC:.cpp=.o)
OBJ_NO_MAIN := $(filter-out src/main.o,$(OBJ))

$(BIN): $(OBJ)
	$(CXX) $(OBJ) -o $(BIN)

$(TEST_BIN): $(TEST_OBJ) $(OBJ_NO_MAIN)
	$(CXX) $(TEST_OBJ) $(OBJ_NO_MAIN) -o $(TEST_BIN)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(TEST_INC) -c $< -o $@

-include $(OBJ:.o=.d) $(TEST_OBJ:.o=.d)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(BIN) $(TEST_BIN) src/*.o src/*.d tests/*.o tests/*.d

.PHONY: test clean