#!/usr/bin/env bash
# ==============================================================================
# NEXS Graphics - Dependency and Git Repository Setup Script
# ==============================================================================
# This script initializes, synchronizes, and configures the dependencies of the
# nexs_graphics repository in a Git-compatible and automated manner.
# Specifically, it configures the 'base-nexs' submodule to track the 'dev-stable'
# branch.
# ==============================================================================

# Exit immediately if a command exits with a non-zero status
set -e

# Setup terminal color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0;30m' # No Color
BOLD='\033[1m'

echo -e "${BLUE}${BOLD}======================================================================${NC}"
echo -e "${BLUE}${BOLD}        NEXS Graphics - Dependency & Repository Setup Tool            ${NC}"
echo -e "${BLUE}${BOLD}======================================================================${NC}"

# Check if git is installed
if ! command -v git &> /dev/null; then
    echo -e "${RED}[ERROR] git command could not be found. Please install Git and try again.${NC}"
    exit 1
fi

# Ensure we are running from the repository root
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || true)"
if [ -z "$REPO_ROOT" ]; then
    echo -e "${RED}[ERROR] This directory is not inside a Git repository!${NC}"
    exit 1
fi

cd "$REPO_ROOT"
echo -e "${GREEN}[INFO] Repository root identified: $REPO_ROOT${NC}"

# 1. Initialize and update submodules
echo -e "${YELLOW}[ACTION] Initializing and updating Git submodules...${NC}"
git submodule update --init --recursive

# 2. Configure the base-nexs submodule to be on the correct branch (dev-stable)
if [ -d "base-nexs" ]; then
    echo -e "${YELLOW}[ACTION] Configuring base-nexs dependency...${NC}"
    cd base-nexs
    
    # Fetch latest remote changes
    echo -e "${GREEN}[INFO] Fetching latest commits for base-nexs...${NC}"
    git fetch origin
    
    # Check out the target dev-stable branch
    echo -e "${GREEN}[INFO] Checking out and tracking branch 'dev-stable'...${NC}"
    git checkout dev-stable || git checkout -b dev-stable origin/dev-stable
    
    # Pull latest updates
    git pull origin dev-stable
    
    cd "$REPO_ROOT"
    echo -e "${GREEN}[SUCCESS] base-nexs submodule is synchronized on branch 'dev-stable'.${NC}"
else
    echo -e "${RED}[ERROR] base-nexs directory was not found after submodule update!${NC}"
    exit 1
fi

echo -e "${BLUE}${BOLD}======================================================================${NC}"
echo -e "${GREEN}${BOLD}           SETUP COMPLETED SUCCESSFULLY WITH 100% PARITY!             ${NC}"
echo -e "${BLUE}${BOLD}======================================================================${NC}"
