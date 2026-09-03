CXX := g++

# Flags por defecto de compilacion. -Wpedantic agrega chequeos del
# estandar; se dejan en "modo aviso" (sin -Werror) para no cortar el build
# ante un aviso de un GCC/Clang nuevo en CI.
# -I. : los includes de capa son rutas relativas ("core/Document.h",
# "ui/Editor.h", "terminal/Event.h"), asi que se compila desde la raiz.
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -I. -MMD -MP -pthread

# El binario final se llama "maestro" y vive en build/: el punto de
# entrada del proyecto para el usuario es el script wrapper ./maestro en
# la raiz (ver README), que decide si compila+abre el editor o delega en
# `make test` / `make test-sanitize` / `make clean`. Compilar el binario
# DENTRO de build/ evita que colisione en el filesystem con ese script.
BIN := build/maestro
# Program sources por capa (modelo / ui / terminal / clipboard / filesystem).
SRC := $(wildcard core/*.cpp ui/*.cpp terminal/*.cpp clipboard/*.cpp filesystem/*.cpp)

# --- Tests ---
# Los tests se agrupan por nivel: unit/ (core puro), interaction/ (usan
# ui/) y e2e/ (flujo completo); los helpers (test_framework.h) viven en
# helpers/. test_main.cpp es el runner en la raiz de tests/.
TEST_DIR := tests
TEST_INC := -I$(TEST_DIR) -I$(TEST_DIR)/helpers
TEST_SRC_ALL := $(TEST_DIR)/test_main.cpp \
            $(wildcard $(TEST_DIR)/unit/*.cpp) \
            $(wildcard $(TEST_DIR)/interaction/*.cpp) \
            $(wildcard $(TEST_DIR)/performance/*.cpp) \
            $(wildcard $(TEST_DIR)/terminal_graphics/*.cpp) \
            $(wildcard $(TEST_DIR)/e2e/*.cpp) \
            $(wildcard $(TEST_DIR)/integration/*.cpp) \
            $(wildcard $(TEST_DIR)/integration/x11_clipboard/*.cpp)
TEST_SRC := $(TEST_DIR)/test_main.cpp \
            $(wildcard $(TEST_DIR)/unit/*.cpp) \
            $(wildcard $(TEST_DIR)/interaction/*.cpp) \
            $(wildcard $(TEST_DIR)/e2e/*.cpp) \
            $(wildcard $(TEST_DIR)/integration/*.cpp) \
            $(wildcard $(TEST_DIR)/integration/x11_clipboard/*.cpp)
TEST_SRC_PERF := $(TEST_DIR)/test_main.cpp \
            $(wildcard $(TEST_DIR)/performance/*.cpp)
TEST_SRC_TERM := $(TEST_DIR)/test_main.cpp \
            $(wildcard $(TEST_DIR)/terminal_graphics/*.cpp)
TEST_BIN := build/edit_tests
TEST_BIN_ALL := build/edit_tests_all
TEST_BIN_PERF := build/edit_tests_performance
TEST_BIN_TERM := build/edit_tests_terminal

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
#   make            build normal: build/maestro
#   make test       compila y ejecuta la suite (build/)
#   make sanitize   build con -fsanitize=address,undefined (build-san/)
#   make test-sanitize  compila y ejecuta la suite sanitizada
#
# El uso recomendado para el dia a dia es el wrapper ./maestro (ver
# raiz del repo): ./maestro <archivo>, ./maestro test, ./maestro
# test-sanitize, ./maestro clean.
OBJ := $(addprefix build/,$(notdir $(SRC:.cpp=.o)))
TEST_OBJ := $(addprefix build/,$(notdir $(TEST_SRC:.cpp=.o)))
TEST_OBJ_ALL := $(addprefix build/,$(notdir $(TEST_SRC_ALL:.cpp=.o)))
TEST_OBJ_PERF := $(addprefix build/,$(notdir $(TEST_SRC_PERF:.cpp=.o)))
TEST_OBJ_TERM := $(addprefix build/,$(notdir $(TEST_SRC_TERM:.cpp=.o)))
# Fuentes del programa enlazadas en los tests (sin el main).
OBJ_NO_MAIN := $(filter-out build/main.o,$(OBJ))

SAN_BIN := build-san/maestro
SAN_TEST_BIN := build-san/edit_tests
SAN_TEST_BIN_ALL := build-san/edit_tests_all
SAN_TEST_BIN_PERF := build-san/edit_tests_performance
SAN_TEST_BIN_TERM := build-san/edit_tests_terminal
SAN_OBJ := $(addprefix build-san/,$(notdir $(SRC:.cpp=.o)))
SAN_TEST_OBJ := $(addprefix build-san/,$(notdir $(TEST_SRC:.cpp=.o)))
SAN_TEST_OBJ_ALL := $(addprefix build-san/,$(notdir $(TEST_SRC_ALL:.cpp=.o)))
SAN_TEST_OBJ_PERF := $(addprefix build-san/,$(notdir $(TEST_SRC_PERF:.cpp=.o)))
SAN_TEST_OBJ_TERM := $(addprefix build-san/,$(notdir $(TEST_SRC_TERM:.cpp=.o)))
SAN_OBJ_NO_MAIN := $(filter-out build-san/main.o,$(SAN_OBJ))

# Flags extra para el build sanitizado (compilacion y link).
SANFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -g

# Los directorios build/ y build-san/ se declaran como prerequisito
# ORDER-ONLY (| build): hacen falta para poder escribir los .o (y los
# binarios finales, que ahora tambien viven adentro), pero su mtime NO
# debe invalidar los objetos. Si fueran prerequisito normal, el
# directorio se actualiza al escribir cada .o y quedaria mas nuevo que los
# objetos anteriores, provocando un rebuild perpetuo en cada `make`.
build/%.o: core/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/%.o: ui/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/%.o: clipboard/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/%.o: terminal/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/%.o: filesystem/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/%.o: tests/%.cpp | build
	$(CXX) $(CXXFLAGS) $(TEST_INC) -c $< -o $@

build/%.o: tests/unit/%.cpp | build
	$(CXX) $(CXXFLAGS) $(TEST_INC) -c $< -o $@

build/%.o: tests/interaction/%.cpp | build
	$(CXX) $(CXXFLAGS) $(TEST_INC) -c $< -o $@

build/%.o: tests/e2e/%.cpp | build
	$(CXX) $(CXXFLAGS) $(TEST_INC) -c $< -o $@

build/%.o: tests/performance/%.cpp | build
	$(CXX) $(CXXFLAGS) $(TEST_INC) -c $< -o $@

build/%.o: tests/terminal_graphics/%.cpp | build
	$(CXX) $(CXXFLAGS) $(TEST_INC) -c $< -o $@

build/%.o: tests/integration/%.cpp | build
	$(CXX) $(CXXFLAGS) $(TEST_INC) -c $< -o $@

build/%.o: tests/integration/x11_clipboard/%.cpp | build
	$(CXX) $(CXXFLAGS) $(TEST_INC) -c $< -o $@

build-san/%.o: core/%.cpp | build-san
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -c $< -o $@

build-san/%.o: ui/%.cpp | build-san
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -c $< -o $@

build-san/%.o: clipboard/%.cpp | build-san
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -c $< -o $@

build-san/%.o: terminal/%.cpp | build-san
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -c $< -o $@

build-san/%.o: filesystem/%.cpp | build-san
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -c $< -o $@

build-san/%.o: tests/%.cpp | build-san
	$(CXX) $(CXXFLAGS) $(SANFLAGS) $(TEST_INC) -c $< -o $@

build-san/%.o: tests/unit/%.cpp | build-san
	$(CXX) $(CXXFLAGS) $(SANFLAGS) $(TEST_INC) -c $< -o $@

build-san/%.o: tests/interaction/%.cpp | build-san
	$(CXX) $(CXXFLAGS) $(SANFLAGS) $(TEST_INC) -c $< -o $@

build-san/%.o: tests/e2e/%.cpp | build-san
	$(CXX) $(CXXFLAGS) $(SANFLAGS) $(TEST_INC) -c $< -o $@

build-san/%.o: tests/performance/%.cpp | build-san
	$(CXX) $(CXXFLAGS) $(SANFLAGS) $(TEST_INC) -c $< -o $@

build-san/%.o: tests/terminal_graphics/%.cpp | build-san
	$(CXX) $(CXXFLAGS) $(SANFLAGS) $(TEST_INC) -c $< -o $@

build-san/%.o: tests/integration/%.cpp | build-san
	$(CXX) $(CXXFLAGS) $(SANFLAGS) $(TEST_INC) -c $< -o $@

build-san/%.o: tests/integration/x11_clipboard/%.cpp | build-san
	$(CXX) $(CXXFLAGS) $(SANFLAGS) $(TEST_INC) -c $< -o $@

# Default (make a secas): compila el editor.
all: $(BIN)

build build-san:
	@mkdir -p $@

$(BIN): $(OBJ) | build
	$(CXX) $(OBJ) -o $(BIN) -lX11 -pthread

$(TEST_BIN): $(TEST_OBJ) $(OBJ_NO_MAIN) | build
	$(CXX) $(TEST_OBJ) $(OBJ_NO_MAIN) -o $(TEST_BIN) -lX11 -pthread

$(TEST_BIN_ALL): $(TEST_OBJ_ALL) $(OBJ_NO_MAIN) | build
	$(CXX) $(TEST_OBJ_ALL) $(OBJ_NO_MAIN) -o $(TEST_BIN_ALL) -lX11 -pthread

$(TEST_BIN_PERF): $(TEST_OBJ_PERF) $(OBJ_NO_MAIN) | build
	$(CXX) $(TEST_OBJ_PERF) $(OBJ_NO_MAIN) -o $(TEST_BIN_PERF) -lX11 -pthread

$(TEST_BIN_TERM): $(TEST_OBJ_TERM) $(OBJ_NO_MAIN) | build
	$(CXX) $(TEST_OBJ_TERM) $(OBJ_NO_MAIN) -o $(TEST_BIN_TERM) -lX11 -pthread

edit-san: $(SAN_OBJ) | build-san
	$(CXX) $(SANFLAGS) $(SAN_OBJ) -o $(SAN_BIN) -lX11 -pthread

test-san: $(SAN_TEST_OBJ) $(SAN_OBJ_NO_MAIN) | build-san
	$(CXX) $(SANFLAGS) $(SAN_TEST_OBJ) $(SAN_OBJ_NO_MAIN) -o $(SAN_TEST_BIN) -lX11 -pthread

test-san-all: $(SAN_TEST_OBJ_ALL) $(SAN_OBJ_NO_MAIN) | build-san
	$(CXX) $(SANFLAGS) $(SAN_TEST_OBJ_ALL) $(SAN_OBJ_NO_MAIN) -o $(SAN_TEST_BIN_ALL) -lX11 -pthread

test-san-perf: $(SAN_TEST_OBJ_PERF) $(SAN_OBJ_NO_MAIN) | build-san
	$(CXX) $(SANFLAGS) $(SAN_TEST_OBJ_PERF) $(SAN_OBJ_NO_MAIN) -o $(SAN_TEST_BIN_PERF) -lX11 -pthread

test-san-term: $(SAN_TEST_OBJ_TERM) $(SAN_OBJ_NO_MAIN) | build-san
	$(CXX) $(SANFLAGS) $(SAN_TEST_OBJ_TERM) $(SAN_OBJ_NO_MAIN) -o $(SAN_TEST_BIN_TERM) -lX11 -pthread

sanitize: edit-san test-san

test: $(TEST_BIN)
	./$(TEST_BIN)

test-all: $(TEST_BIN_ALL)
	./$(TEST_BIN_ALL)

test-performance: $(TEST_BIN_PERF)
	./$(TEST_BIN_PERF)

test-terminal-graphics: $(TEST_BIN_TERM)
	./$(TEST_BIN_TERM)

# AVISO: compilar la suite sanitizada es LENTO (cada .o se compila dos
# veces, la segunda con ASan/UBSan). No correrlo por defecto; solo cuando
# lo pida el usuario o lo exija el CI.
test-sanitize: test-san
	./$(SAN_TEST_BIN)

test-sanitize-all: test-san-all
	./$(SAN_TEST_BIN_ALL)

test-sanitize-performance: test-san-perf
	./$(SAN_TEST_BIN_PERF)

test-sanitize-terminal-graphics: test-san-term
	./$(SAN_TEST_BIN_TERM)

INSTALL_DIR := $(HOME)/.local/bin
INSTALL_BIN := $(INSTALL_DIR)/maestro

install: $(BIN)
	@mkdir -p $(INSTALL_DIR)
	@install -m 755 $(BIN) $(INSTALL_BIN)
	@echo "Installed to $(INSTALL_BIN)"

# uninstall elimina solo $(INSTALL_BIN); no borra $(INSTALL_DIR) para
# no afectar otros binarios del usuario en ~/.local/bin
uninstall:
	@rm -f $(INSTALL_BIN)
	@echo "Uninstalled $(INSTALL_BIN)"

clean:
	rm -rf build build-san

-include $(OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(TEST_OBJ_ALL:.o=.d) $(TEST_OBJ_PERF:.o=.d) $(TEST_OBJ_TERM:.o=.d) $(SAN_OBJ:.o=.d) $(SAN_TEST_OBJ:.o=.d) $(SAN_TEST_OBJ_ALL:.o=.d) $(SAN_TEST_OBJ_PERF:.o=.d) $(SAN_TEST_OBJ_TERM:.o=.d)

.PHONY: all test test-all test-performance test-terminal-graphics sanitize test-sanitize test-sanitize-all test-sanitize-performance test-sanitize-terminal-graphics clean edit-san test-san test-san-all test-san-perf test-san-term install uninstall
