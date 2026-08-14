CXX := g++

# Flags por defecto de compilacion. -Wpedantic agrega chequeos del
# estandar; se dejan en "modo aviso" (sin -Werror) para no cortar el build
# ante un aviso de un GCC/Clang nuevo en CI.
# -I. : los includes de capa son rutas relativas ("core/Document.h",
# "ui/Editor.h", "terminal/Event.h"), asi que se compila desde la raiz.
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -I. -MMD -MP

BIN := edit
# Program sources por capa (modelo / ui / terminal).
SRC := $(wildcard core/*.cpp ui/*.cpp terminal/*.cpp)

# --- Tests ---
TEST_DIR := tests
TEST_INC := -I$(TEST_DIR)
TEST_SRC := $(TEST_DIR)/test_main.cpp \
            $(TEST_DIR)/test_document.cpp \
            $(TEST_DIR)/test_cursor.cpp \
            $(TEST_DIR)/test_event.cpp \
            $(TEST_DIR)/test_terminal_event.cpp \
            $(TEST_DIR)/test_editor.cpp \
            $(TEST_DIR)/test_keymap.cpp \
            $(TEST_DIR)/test_selection.cpp \
            $(TEST_DIR)/test_renderer.cpp \
            $(TEST_DIR)/test_modes.cpp \
            $(TEST_DIR)/test_invariants.cpp \
            $(TEST_DIR)/test_utf8.cpp \
            $(TEST_DIR)/test_truncate.cpp \
            $(TEST_DIR)/test_utf8range.cpp \
            $(TEST_DIR)/test_clipboard.cpp \
            $(TEST_DIR)/test_buffers.cpp \
            $(TEST_DIR)/test_filebrowser.cpp \
            $(TEST_DIR)/test_roundtrip.cpp
TEST_BIN := edit_tests

# --- Build normal (build/) y sanitizado (build-san/) ---
# Compilar con -fsanitize requiere que TODOS los objetos (y el link) usen
# las flags de sanitizer. Reusar los .o limpios mezclaria objetos
# sanitizados y no-sanitizados, que es incorrecto e indefinido. Por eso
# cada configuración escribe sus .o/.d en su propio directorio.
#
# Los objetos son PLANOS en build/ (basename): los nombres de .cpp son
# unicos entre capas (Document.cpp solo en core/, Editor.cpp solo en ui/,
# Terminal.cpp solo en terminal/), asi que no hay colisiones.
#
# Uso:
#   make            build normal: edit + edit_tests
#   make test       compila y ejecuta la suite (build/)
#   make sanitize   build con -fsanitize=address,undefined (build-san/)
#   make test-sanitize  compila y ejecuta la suite sanitizada
OBJ := $(addprefix build/,$(notdir $(SRC:.cpp=.o)))
TEST_OBJ := $(addprefix build/,$(notdir $(TEST_SRC:.cpp=.o)))
# Fuentes del programa enlazadas en los tests (sin el main).
OBJ_NO_MAIN := $(filter-out build/main.o,$(OBJ))

SAN_OBJ := $(addprefix build-san/,$(notdir $(SRC:.cpp=.o)))
SAN_TEST_OBJ := $(addprefix build-san/,$(notdir $(TEST_SRC:.cpp=.o)))
SAN_OBJ_NO_MAIN := $(filter-out build-san/main.o,$(SAN_OBJ))

# Flags extra para el build sanitizado (compilacion y link).
SANFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -g

build/%.o: core/%.cpp build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/%.o: ui/%.cpp build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/%.o: terminal/%.cpp build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/%.o: tests/%.cpp build
	$(CXX) $(CXXFLAGS) $(TEST_INC) -c $< -o $@

build-san/%.o: core/%.cpp build-san
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -c $< -o $@

build-san/%.o: ui/%.cpp build-san
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -c $< -o $@

build-san/%.o: terminal/%.cpp build-san
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -c $< -o $@

build-san/%.o: tests/%.cpp build-san
	$(CXX) $(CXXFLAGS) $(SANFLAGS) $(TEST_INC) -c $< -o $@

build build-san:
	@mkdir -p $@

$(BIN): $(OBJ)
	$(CXX) $(OBJ) -o $(BIN)

$(TEST_BIN): $(TEST_OBJ) $(OBJ_NO_MAIN)
	$(CXX) $(TEST_OBJ) $(OBJ_NO_MAIN) -o $(TEST_BIN)

edit-san: $(SAN_OBJ)
	$(CXX) $(SANFLAGS) $(SAN_OBJ) -o $(BIN)-san

test-san: $(SAN_TEST_OBJ) $(SAN_OBJ_NO_MAIN)
	$(CXX) $(SANFLAGS) $(SAN_TEST_OBJ) $(SAN_OBJ_NO_MAIN) -o $(TEST_BIN)-san

sanitize: edit-san test-san

test: $(TEST_BIN)
	./$(TEST_BIN)

# AVISO: compilar la suite sanitizada es LENTO (cada .o se compila dos
# veces, la segunda con ASan/UBSan). No correrlo por defecto; solo cuando
# lo pida el usuario o lo exija el CI.
test-sanitize: test-san
	./$(TEST_BIN)-san

clean:
	rm -rf build build-san $(BIN) $(TEST_BIN) $(BIN)-san $(TEST_BIN)-san

-include $(OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(SAN_OBJ:.o=.d) $(SAN_TEST_OBJ:.o=.d)

.PHONY: test sanitize test-sanitize clean