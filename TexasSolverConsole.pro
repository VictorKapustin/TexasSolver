#-------------------------------------------------
# Project for TexasSolver Console (CLI)
#-------------------------------------------------

QT       += core
QT       -= gui

TARGET = TexasSolverConsole
TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle

win32: {
    QMAKE_CXXFLAGS += -fopenmp
    QMAKE_LFLAGS += -fopenmp
}

# Optimization flags from optimized GUI build
QMAKE_CXXFLAGS_RELEASE -= -O2
QMAKE_CXXFLAGS_RELEASE *= -O3 -march=native

CONFIG(enable_lto, enable_lto|disable_lto) {
    QMAKE_CXXFLAGS_RELEASE += -flto
    QMAKE_LFLAGS_RELEASE += -flto
}

CONFIG(pgo_generate, pgo_generate|pgo_use) {
    QMAKE_CXXFLAGS_RELEASE += -fprofile-generate
    QMAKE_LFLAGS_RELEASE += -fprofile-generate
}

CONFIG(pgo_use, pgo_generate|pgo_use) {
    QMAKE_CXXFLAGS_RELEASE += -fprofile-use -fprofile-correction
    QMAKE_LFLAGS_RELEASE += -fprofile-use -fprofile-correction
}

# Deployment automation
win32: CONFIG(release, debug|release) {
    QMAKE_POST_LINK += $$quote(copy /y C:\Qt\Tools\mingw810_64\bin\libgomp-1.dll release)
}

INCLUDEPATH += . include

SOURCES += \
    src/console.cpp \
    src/Deck.cpp \
    src/Card.cpp \
    src/GameTree.cpp \
    src/library.cpp \
    src/compairer/Dic5Compairer.cpp \
    src/experimental/TCfrSolver.cpp \
    src/nodes/ActionNode.cpp \
    src/nodes/ChanceNode.cpp \
    src/nodes/GameActions.cpp \
    src/nodes/GameTreeNode.cpp \
    src/nodes/ShowdownNode.cpp \
    src/nodes/TerminalNode.cpp \
    src/ranges/PrivateCards.cpp \
    src/ranges/PrivateCardsManager.cpp \
    src/ranges/RiverCombs.cpp \
    src/ranges/RiverRangeManager.cpp \
    src/runtime/PokerSolver.cpp \
    src/solver/BestResponse.cpp \
    src/solver/CfrSolver.cpp \
    src/solver/PCfrSolver.cpp \
    src/solver/Solver.cpp \
    src/tools/CommandLineTool.cpp \
    src/tools/GameTreeBuildingSettings.cpp \
    src/tools/lookup8.cpp \
    src/tools/PrivateRangeConverter.cpp \
    src/tools/progressbar.cpp \
    src/tools/Rule.cpp \
    src/tools/StreetSetting.cpp \
    src/tools/utils.cpp \
    src/trainable/CfrPlusTrainable.cpp \
    src/trainable/DiscountedCfrTrainable.cpp \
    src/trainable/DiscountedCfrTrainableHF.cpp \
    src/trainable/DiscountedCfrTrainableSF.cpp \
    src/trainable/Trainable.cpp

HEADERS += \
    include/tools/CommandLineTool.h \
    include/tools/argparse.hpp \
    include/runtime/PokerSolver.h \
    include/Card.h \
    include/GameTree.h \
    include/Deck.h \
    include/json.hpp \
    include/library.h \
    include/solver/PCfrSolver.h \
    include/solver/Solver.h \
    include/solver/BestResponse.h \
    include/solver/CfrSolver.h \
    include/tools/utils.h \
    include/tools/GameTreeBuildingSettings.h \
    include/tools/Rule.h \
    include/tools/StreetSetting.h \
    include/tools/lookup8.h \
    include/tools/PrivateRangeConverter.h \
    include/tools/progressbar.h \
    include/trainable/CfrPlusTrainable.h \
    include/trainable/DiscountedCfrTrainable.h \
    include/trainable/Trainable.h \
    include/compairer/Compairer.h \
    include/compairer/Dic5Compairer.h \
    include/experimental/TCfrSolver.h \
    include/nodes/ActionNode.h \
    include/nodes/ChanceNode.h \
    include/nodes/GameActions.h \
    include/nodes/GameTreeNode.h \
    include/nodes/ShowdownNode.h \
    include/nodes/TerminalNode.h \
    include/ranges/PrivateCards.h \
    include/ranges/PrivateCardsManager.h \
    include/ranges/RiverCombs.h \
    include/ranges/RiverRangeManager.h \
    include/tools/tinyformat.h
