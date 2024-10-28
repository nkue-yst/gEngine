cmake_minimum_required(VERSION 3.16)

set(HEADERS
    SGameInstance.h
)

set(SOURCES
    SGameInstance.cpp
)

add_library(SGameInstance STATIC ${HEADERS} ${SOURCES})
