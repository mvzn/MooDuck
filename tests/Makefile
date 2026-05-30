# Headless DSP test runner. Builds standalone — does not touch the plugin build.
# Run: make && ./runtests
#
# Expects JUCE at /home/mokram/JUCE/modules (override with `make JUCE_MODULES=…`).

JUCE_MODULES ?= /home/mokram/JUCE/modules

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wno-unused-parameter -Wno-deprecated-declarations \
            -pthread -fvisibility=hidden

# Headless module set: drop GUI, audio_devices, plugin client. tests/JuceHeader.h
# matches this exact list and is found first via -I tests/.
DEFS := \
    -DLINUX=1 -DNDEBUG=1 \
    -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
    -DJUCE_STANDALONE_APPLICATION=0 \
    -DJUCE_MODULE_AVAILABLE_juce_core=1 \
    -DJUCE_MODULE_AVAILABLE_juce_events=1 \
    -DJUCE_MODULE_AVAILABLE_juce_data_structures=1 \
    -DJUCE_MODULE_AVAILABLE_juce_audio_basics=1 \
    -DJUCE_MODULE_AVAILABLE_juce_audio_formats=1 \
    -DJUCE_MODULE_AVAILABLE_juce_dsp=1

INCLUDES := -I. -I$(JUCE_MODULES) -I../Source -I../Source/vendor

LDLIBS := -lpthread -ldl -lrt -lcurl

OBJDIR := build
OBJS := \
    $(OBJDIR)/juce_core.o \
    $(OBJDIR)/juce_events.o \
    $(OBJDIR)/juce_data_structures.o \
    $(OBJDIR)/juce_audio_basics.o \
    $(OBJDIR)/juce_audio_formats.o \
    $(OBJDIR)/juce_dsp.o \
    $(OBJDIR)/juce_core_CompilationTime.o \
    $(OBJDIR)/runtests.o

runtests: $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) $(LDLIBS) -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/runtests.o: runtests.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(DEFS) $(INCLUDES) -c $< -o $@

$(OBJDIR)/juce_core.o: | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(DEFS) $(INCLUDES) -x c++ -c $(JUCE_MODULES)/juce_core/juce_core.cpp -o $@

$(OBJDIR)/juce_events.o: | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(DEFS) $(INCLUDES) -x c++ -c $(JUCE_MODULES)/juce_events/juce_events.cpp -o $@

$(OBJDIR)/juce_data_structures.o: | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(DEFS) $(INCLUDES) -x c++ -c $(JUCE_MODULES)/juce_data_structures/juce_data_structures.cpp -o $@

$(OBJDIR)/juce_audio_basics.o: | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(DEFS) $(INCLUDES) -x c++ -c $(JUCE_MODULES)/juce_audio_basics/juce_audio_basics.cpp -o $@

$(OBJDIR)/juce_audio_formats.o: | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(DEFS) $(INCLUDES) -x c++ -c $(JUCE_MODULES)/juce_audio_formats/juce_audio_formats.cpp -o $@

$(OBJDIR)/juce_dsp.o: | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(DEFS) $(INCLUDES) -x c++ -c $(JUCE_MODULES)/juce_dsp/juce_dsp.cpp -o $@

$(OBJDIR)/juce_core_CompilationTime.o: | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(DEFS) $(INCLUDES) -x c++ -c $(JUCE_MODULES)/juce_core/juce_core_CompilationTime.cpp -o $@

clean:
	rm -rf $(OBJDIR) runtests

.PHONY: clean
