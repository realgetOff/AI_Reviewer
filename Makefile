NAME          = ai_reviewer
CLEAN_SCRIPT = ./clean_reports.sh

CC            = g++
CXXFLAGS      = -Wall -Wextra -Werror -std=c++17 -pthread
INCLUDES      = -I includes -I $(HOME)/libs/install/include
LDFLAGS       = -L $(HOME)/libs/install/lib -lcurl -pthread

SRCS_DIR      = srcs
OBJS_DIR      = objs

SRCS          = $(SRCS_DIR)/main.cpp \
				$(SRCS_DIR)/ai_client.cpp \
				$(SRCS_DIR)/update.cpp \
				$(SRCS_DIR)/utils.cpp

OBJS          = $(SRCS:$(SRCS_DIR)/%.cpp=$(OBJS_DIR)/%.o)

TOTAL_FILES   := $(words $(SRCS))
CURRENT_FILE  = 0

G             = \033[1;92m
Y             = \033[1;93m
R             = \033[0m

all:          $(NAME)

$(NAME):      $(OBJS)
	@printf "\n$(Y)Linking $(NAME)...$(R)\n"
	@$(CC) $(CXXFLAGS) $(OBJS) $(LDFLAGS) -o $(NAME)
	@echo "$(G)Build successful!$(R)"

$(OBJS_DIR)/%.o: $(SRCS_DIR)/%.cpp
	@mkdir -p $(OBJS_DIR)
	@$(eval CURRENT_FILE=$(shell echo $$(($(CURRENT_FILE) + 1))))
	@$(eval PERCENT=$(shell echo $$(($(CURRENT_FILE) * 100 / $(TOTAL_FILES)))))
	@$(eval PROGRESS=$(shell echo $$(($(CURRENT_FILE) * 30 / $(TOTAL_FILES)))))
	@printf "\r$(Y)Compiling:  [$(G)%-30s$(Y)] %d%% (%d/%d)$(R)" \
		"$$(printf '%.0s=' $$(seq 1 $(PROGRESS)))" \
		"$(PERCENT)" "$(CURRENT_FILE)" "$(TOTAL_FILES)"
	@$(CC) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	@printf "$(Y)Cleaning objects...$(R)\n"
	@$(RM) -r $(OBJS_DIR)

fclean: clean clean_reports
	@printf "$(Y)Cleaning binary...$(R)\n"
	@$(RM) $(NAME)

clean_reports:
	@$(CLEAN_SCRIPT)

re:           fclean all

.PHONY:       all clean fclean re clean_reports
