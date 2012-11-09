FLAGS+=$(shell cups-config --cflags --libs)
FLAGS+=$(shell pkg-config --cflags --libs libqpdf)
CXXFLAGS+=-Wall

all: pdfautorotate

pdfautorotate: pdfautorotate.cpp
	$(CXX) $^ -o $@ $(CXXFLAGS) $(FLAGS)

clean:
	-rm pdfautorotate
