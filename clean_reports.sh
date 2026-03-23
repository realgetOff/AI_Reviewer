#!/bin/bash

REPORT_DIR="reports"

if [ -d "$REPORT_DIR" ]; then
    rm -rf "$REPORT_DIR"/*
else
    mkdir -p "$REPORT_DIR"
fi
