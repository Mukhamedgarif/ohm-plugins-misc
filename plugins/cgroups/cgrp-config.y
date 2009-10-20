%{

#include <stdio.h>

#include "cgrp-plugin.h"
#include "cgrp-parser-types.h"
#include "mm.h"  

int        cgrpyylex  (void);
void       cgrpyyerror(cgrp_context_t *, const char *);
extern int lexer_start_token;

%}

%union {
    token_string_t    any;
    token_string_t    string;
    token_uint32_t    uint32;
    token_sint32_t    sint32;
    cgrp_partition_t  part;
    cgrp_partition_t *parts;
    cgrp_group_t      group;
    cgrp_group_t     *groups;
    cgrp_procdef_t    rule;
    cgrp_procdef_t   *rules;
    cgrp_stmt_t      *stmt;
    cgrp_expr_t      *expr;
    cgrp_cmd_t        cmd;
    cgrp_prop_type_t  prop;
    cgrp_value_t      value;
    cgrp_context_t    ctx;
    int               renice;
}

%defines
%parse-param {cgrp_context_t *ctx}

%type <part>     partition
%type <part>     partition_properties
%type <string>   partition_path
%type <string>   path
%type <uint32>   partition_cpu_share
%type <uint32>   partition_mem_limit
%type <uint32>   optional_unit
%type <group>    group
%type <group>    group_properties
%type <group>    group_description
%type <group>    group_partition
%type <rule>     rule
%type <string>   rule_path
%type <stmt>     rule_statements
%type <stmt>     rule_statement
%type <expr>     expr
%type <expr>     bool_expr
%type <expr>     prop_expr
%type <prop>     prop
%type <value>    value
%type <cmd>      command
%type <cmd>      command_group
%type <cmd>      command_ignore
%type <cmd>      command_reclassify
%type <string>   group_name
%type <string>   string
%type <renice>   optional_renice

%token START_FULL_PARSER
%token START_ADDON_PARSER

%token KEYWORD_GLOBAL
%token KEYWORD_PARTITION
%token KEYWORD_DESCRIPTION
%token KEYWORD_PATH
%token KEYWORD_CPU_SHARES
%token KEYWORD_MEM_LIMIT
%token KEYWORD_RULE
%token KEYWORD_BINARY
%token KEYWORD_CMDLINE
%token KEYWORD_GROUP
%token KEYWORD_RENICE
%token KEYWORD_USER
%token KEYWORD_PARENT
%token KEYWORD_TYPE
%token KEYWORD_IGNORE
%token KEYWORD_RECLASSIFY
%token KEYWORD_RECLASS_AFTER
%token KEYWORD_EXPORT_GROUPS
%token KEYWORD_EXPORT_PARTITIONS
%token KEYWORD_EXPORT_FACT
%token KEYWORD_CGROUPFS_OPTIONS
%token KEYWORD_IOWAIT_NOTIFY
%token KEYWORD_SWAP_PRESSURE
%token KEYWORD_ADDON_RULES

%token TOKEN_EOL "\n"
%token TOKEN_ASTERISK "*"
%token TOKEN_HEADER_OPEN "["
%token TOKEN_HEADER_CLOSE "]"

%token TOKEN_AND   "&&"
%token TOKEN_OR    "||"
%token TOKEN_NOT   "!"
%token TOKEN_EQUAL "=="
%token TOKEN_NOTEQ "!="
%token TOKEN_LESS  "<"
%token TOKEN_IMPLIES "=>"

%token <uint32> TOKEN_ARG
%token <string> TOKEN_IDENT
%token <string> TOKEN_PATH
%token <string> TOKEN_STRING
%token <uint32> TOKEN_UINT
%token <sint32> TOKEN_SINT

%%

configuration: START_FULL_PARSER 
                 global_section partition_section group_section rule_section
    | START_ADDON_PARSER
                 rule_section
    ;


/*****************************************************************************
 *                            *** global section ***                         *
 *****************************************************************************/

global_section: /* empty: whole section can be omitted */
    | KEYWORD_GLOBAL "\n" global_options
    ;

global_options: /* empty: allow just the header without any actual options */
    | global_option 
    | global_options global_option
    ;

global_option: KEYWORD_EXPORT_GROUPS "\n" {
	  CGRP_SET_FLAG(ctx->options.flags, CGRP_FLAG_GROUP_FACTS);
    }
    | KEYWORD_EXPORT_PARTITIONS "\n" {
          CGRP_SET_FLAG(ctx->options.flags, CGRP_FLAG_PART_FACTS);
    }
    | iowait_notify "\n"
    | swap_pressure "\n"
    | cgroupfs_options "\n"
    | addon_rules "\n"
    | error {
        OHM_ERROR("cgrp: failed to parse global options near token '%s'",
                  cgrpyylval.any.token);
        exit(1);
    }
    ;

iowait_notify: KEYWORD_IOWAIT_NOTIFY iowait_notify_options
    ;

iowait_notify_options: iowait_notify_option
    | iowait_notify_options iowait_notify_option
    ;

iowait_notify_option: TOKEN_IDENT TOKEN_UINT TOKEN_UINT {
          if (!strcmp($1.value, "threshold")) {
              ctx->iow.thres_low  = $2.value;
              ctx->iow.thres_high = $3.value;
          }
          else if (!strcmp($1.value, "poll")) {
              ctx->iow.poll_high = $2.value;
              ctx->iow.poll_low  = $3.value;
          }
          else {
              OHM_ERROR("cgrp: invalid iowait-notify parameter %s", $1.value);
	      YYABORT;
          }
    }
    | TOKEN_IDENT TOKEN_UINT {
          if (!strcmp($1.value, "startup-delay"))
              ctx->iow.startup_delay = $2.value;
          else {
              ctx->iow.nsample = $2.value;
              ctx->iow.estim   = estim_alloc($1.value, $2.value);
              if (ctx->iow.estim == NULL)
                  YYABORT;
          }
    }
    | TOKEN_IDENT string {
          if (!strcmp($1.value, "hook"))
              ctx->iow.hook = STRDUP($2.value);
          else {
              OHM_ERROR("cgrp: invalid iowait-notify parameter %s", $1.value);
	      YYABORT;
          }
    }
    | error { 
          OHM_ERROR("cgrp: failed to parse I/O wait options near token '%s'",
                    cgrpyylval.any.token);
          exit(1);
    }
    ;

swap_pressure: KEYWORD_SWAP_PRESSURE swap_pressure_options
    ;

swap_pressure_options: swap_pressure_option
    | swap_pressure_options swap_pressure_option
    ;

swap_pressure_option: TOKEN_IDENT TOKEN_UINT TOKEN_UINT {
          if (strcmp($1.value, "threshold")) {
              OHM_ERROR("cgrp: invalid swap-pressure parameter %s", $1.value);
	      YYABORT;
          }
          ctx->swp.low  = $2.value;
          ctx->swp.high = $3.value;
    }
    | TOKEN_IDENT TOKEN_UINT {
          if (!strcmp($1.value, "min-delay"))
              ctx->swp.interval = $2.value;
          else {
              OHM_ERROR("cgrp: invalid swap-pressure parameter %s", $1.value);
	      YYABORT;
          }
    }
    | TOKEN_IDENT string {
          if (!strcmp($1.value, "hook"))
              ctx->swp.hook = STRDUP($2.value);
          else {
              OHM_ERROR("cgrp: invalid swap-pressure parameter %s", $1.value);
	      YYABORT;
          }
    }
    ;

cgroupfs_options: KEYWORD_CGROUPFS_OPTIONS mount_options
    ;

mount_options: TOKEN_IDENT      { cgroup_set_option(ctx, $1.value); }
    | mount_options TOKEN_IDENT { cgroup_set_option(ctx, $2.value); }
    ;

addon_rules: KEYWORD_ADDON_RULES path {
          ctx->options.addon_rules = STRDUP($2.value);
    }
    ;


/*****************************************************************************
 *                           *** partition section ***                       *
 *****************************************************************************/

partition_section: partition
    | partition_section partition
    ;

partition: "[" KEYWORD_PARTITION TOKEN_IDENT "]" "\n" partition_properties {
      $6.name = $3.value;
      if (partition_lookup(ctx, $6.name) != NULL) {
          OHM_ERROR("cgrp: partition '%s' multiply defined", $6.name);
	  YYABORT;
      }
      if (!strcmp($6.name, "root")) {
          OHM_ERROR("cgrp: invalid partition with reserved name 'root'");
          YYABORT;
      }
      if (partition_add(ctx, &$6) == NULL)
          YYABORT;
    }
    ;

partition_properties: partition_path "\n" {
          $$.path = $1.value;
    }
    | partition_export_fact "\n" {
          CGRP_SET_FLAG($$.flags, CGRP_PARTITION_FACT);
    }
    | partition_cpu_share "\n" {
          $$.limit.cpu = $1.value;
    }
    | partition_mem_limit "\n" {
          $$.limit.mem = $1.value;
    }
    | partition_properties partition_path "\n" {
          $$ = $1;
          $$.path = $2.value;
    }
    | partition_properties partition_export_fact "\n" {
          $$ = $1;
          CGRP_SET_FLAG($$.flags, CGRP_PARTITION_FACT);
    }
    | partition_properties  partition_cpu_share "\n" {
          $$           = $1;
          $$.limit.cpu = $2.value;
    }
    | partition_properties partition_mem_limit "\n" {
          $$           = $1;
          $$.limit.mem = $2.value;
    }
    | partition_properties error {
        OHM_ERROR("cgrp: failed to parse partition properties near token '%s'",
		  cgrpyylval.any.token);
        exit(1);
    }
    ;

partition_path: KEYWORD_PATH path { $$ = $2; }
    ;

partition_export_fact: KEYWORD_EXPORT_FACT
    ;

partition_cpu_share: KEYWORD_CPU_SHARES TOKEN_UINT { $$ = $2; }
    ;

partition_mem_limit: KEYWORD_MEM_LIMIT TOKEN_UINT optional_unit {
          $$        = $2;
	  $$.value *= $3.value;
    }
    ;


/*****************************************************************************
 *                             *** group section ***                         *
 *****************************************************************************/

group_section: group
    | group_section group
    ;

group: "[" KEYWORD_GROUP TOKEN_IDENT "]" "\n" group_properties {
      $6.name = $3.value;
      if (!group_add(ctx, &$6))
          YYABORT;
    }
    ;

group_properties:      group_description "\n" { $$ = $1; }
    |                  group_partition "\n" { $$ = $1; }
    |                  group_fact "\n" {
        CGRP_SET_FLAG($$.flags, CGRP_GROUPFLAG_FACT);
    }
    | group_properties group_description "\n" {
        $$             = $1;
        $$.description = $2.description;
    }
    | group_properties group_partition "\n" {
        $$            = $1;
        $$.partition  = $2.partition;
	CGRP_SET_FLAG($$.flags, CGRP_GROUPFLAG_STATIC);
    }
    | group_properties group_fact "\n" {
        $$        = $1;
        CGRP_SET_FLAG($$.flags, CGRP_GROUPFLAG_FACT);
    }
    | group_properties error {
        OHM_ERROR("cgrp: failed to parse group properties near token '%s'",
		  cgrpyylval.any.token);
        exit(1);
    }
    ;

group_description: KEYWORD_DESCRIPTION TOKEN_STRING {
        memset(&$$, 0, sizeof($$));
        $$.description = $2.value;
    }
    ;

group_partition: KEYWORD_PARTITION TOKEN_IDENT {
        memset(&$$, 0, sizeof($$));
	if (($$.partition = partition_lookup(ctx, $2.value)) == NULL) {
	    OHM_ERROR("cgrp: nonexisting partition '%s' in a group", $2.value);
	    exit(1);
	}
    }
    ;

group_fact: KEYWORD_EXPORT_FACT
    ;


/*****************************************************************************
 *                             *** rule section ***                          *
 *****************************************************************************/

rule_section: rule
    | rule_section rule
    | rule_section error {
          OHM_ERROR("cgrp: failed to parse rule section near token '%s'",
	            cgrpyylval.any.token);
          exit(1);
    }
    ;

rule: "[" KEYWORD_RULE rule_path "]" "\n" optional_renice rule_statements {
        cgrp_procdef_t rule;

        rule.binary     = $3.value;
	rule.renice     = $6;
        rule.statements = $7;
        if (!procdef_add(ctx, &rule))
	    YYABORT;
    }
    ;

optional_renice: /* empty */    { $$ = 0;  }
    | KEYWORD_RENICE TOKEN_SINT { $$ = $2.value; }
    ;

rule_path: TOKEN_PATH          { $$ = $1; }
    |      TOKEN_STRING        { $$ = $1; }
    |      TOKEN_ASTERISK      { $$.value = "*"; }
    ;

rule_statements: rule_statement "\n" {
        $$ = $1;
    }
    | rule_statements rule_statement "\n" {
        cgrp_stmt_t *stmt;

        for (stmt = $1; stmt->next != NULL; stmt = stmt->next)
	    ;
	stmt->next = $2;
        $$         = $1;
    }
    | rule_statements error {
          OHM_ERROR("cgrp: failed to parse rule statements real '%s'",
                    cgrpyylval.any.token);
          exit(1);
    }
    ;

rule_statement: expr "=>" command {
        cgrp_stmt_t *stmt;

        if (ALLOC_OBJ(stmt) == NULL) {
            OHM_ERROR("cgrp: failed to allocate statement");
            exit(1);
        }
        stmt->expr    = $1;
        stmt->command = $3;

        $$ = stmt;
    }
    | command {
        cgrp_stmt_t *stmt;

        if (ALLOC_OBJ(stmt) == NULL) {
            OHM_ERROR("cgrp: failed to allocate statement");
            exit(1);
        }
        stmt->expr    = NULL;
        stmt->command = $1;

        $$ = stmt;
    }
    ;

expr: bool_expr    { $$ = $1; }
    | prop_expr    { $$ = $1; }
    | "(" expr ")" { $$ = $2; }
    ;

bool_expr: expr "||" expr { $$ = bool_expr(CGRP_BOOL_OR , $1, $3);   }
    |      expr "&&" expr { $$ = bool_expr(CGRP_BOOL_AND, $1, $3);   }
    |      "!" expr       { $$ = bool_expr(CGRP_BOOL_NOT, $2, NULL); }
    ;

prop_expr: prop "==" value { $$ = prop_expr($1, CGRP_OP_EQUAL, &$3); }
    |      prop "!=" value { $$ = prop_expr($1, CGRP_OP_NOTEQ, &$3); }
    |      prop "<"  value { $$ = prop_expr($1, CGRP_OP_LESS, &$3); }
    ;

prop: TOKEN_ARG          { $$ = CGRP_PROP_ARG($1.value); }
    | KEYWORD_BINARY     { $$ = CGRP_PROP_BINARY;        }
    | KEYWORD_CMDLINE    { $$ = CGRP_PROP_CMDLINE;       }
    | KEYWORD_TYPE       { $$ = CGRP_PROP_TYPE;          }
    | KEYWORD_USER       { $$ = CGRP_PROP_EUID;          }
    | KEYWORD_GROUP      { $$ = CGRP_PROP_EGID;          }
    | KEYWORD_PARENT     { $$ = CGRP_PROP_PARENT;        }
    | KEYWORD_RECLASSIFY { $$ = CGRP_PROP_RECLASSIFY;    }
    ;

value: TOKEN_STRING {
        $$.type = CGRP_VALUE_TYPE_STRING;
	$$.str  = STRDUP($1.value);
    }
    | TOKEN_IDENT {
        $$.type = CGRP_VALUE_TYPE_STRING;
	$$.str  = STRDUP($1.value);
    }
    | TOKEN_PATH {
        $$.type = CGRP_VALUE_TYPE_STRING;
	$$.str  = STRDUP($1.value);
    }
    | TOKEN_UINT {
        $$.type = CGRP_VALUE_TYPE_UINT32;
        $$.u32  = $1.value;
    }
    | TOKEN_SINT {
        $$.type = CGRP_VALUE_TYPE_SINT32;
        $$.s32  = $1.value;
    }
    ;

command: command_group   { $$ = $1; }
    | command_ignore     { $$ = $1; }
    | command_reclassify { $$ = $1; }
    ;

command_group: KEYWORD_GROUP group_name {
        cgrp_group_t *group;

        if ((group = group_find(ctx, $2.value)) == NULL) {
            OHM_ERROR("cgrp: reference to nonexisting group \"%s\"", $2.value);
            exit(1);
        }
        $$.group.type  = CGRP_CMD_GROUP;
	$$.group.group = group;
    }
    ;

group_name: TOKEN_STRING { $$ = $1; }
    | TOKEN_IDENT        { $$ = $1; }
    ;

command_ignore: KEYWORD_IGNORE { $$.ignore.type = CGRP_CMD_IGNORE; }
    ;

command_reclassify: KEYWORD_RECLASS_AFTER TOKEN_UINT {
          $$.reclassify.type  = CGRP_CMD_RECLASSIFY;
	  $$.reclassify.delay = $2.value;
    }
    ;



/*****************************************************************************
 *                        *** miscallaneous rules ***                        *
 *****************************************************************************/

string: TOKEN_IDENT  { $$ = $1; }
    |   TOKEN_STRING { $$ = $1; }
    ;


path: TOKEN_PATH {
          if ($1.value[0] != '/') {
              OHM_ERROR("cgrp: invalid path '%s'", $1.value);
              exit(1);
          }
          $$ = $1;
    }
    | TOKEN_STRING {
          if ($1.value[0] != '/') {
              OHM_ERROR("cgrp: invalid path '%s'", $1.value);
              exit(1);
          }
          $$ = $1;
    }
    ;


optional_unit: /* empty */ { $$.value = 1; }
    | TOKEN_IDENT         {
          if ($1.value[1] != '\0')
              goto invalid;

          switch ($1.value[0]) {
              case 'k': case 'K': $$.value = 1024;        break;
              case 'm': case 'M': $$.value = 1024 * 1024; break;
              default:
              invalid:
	          OHM_ERROR("cgrp: invalid memory limit unit '%s'", $1.value);
	          exit(1);
          }
    }
    ;




%%

/********************
 * config_parse_config
 ********************/
int
config_parse_config(cgrp_context_t *ctx, const char *path)
{
    lexer_start_token = START_FULL_PARSER;

    if (!lexer_push_input(path))
	return FALSE;

    return cgrpyyparse(ctx) == 0;
}


/********************
 * config_parse_addons
 ********************/
int
config_parse_addons(cgrp_context_t *ctx)
{
    char *rules = ctx->options.addon_rules;
    char  path[PATH_MAX];
    int   success;

    if (rules == NULL)
        return TRUE;
    
    if (strchr(rules, '*'))
        snprintf(path, sizeof(path), rules);
    else
        snprintf(path, sizeof(path), "%s/*.conf", rules);

    lexer_start_token = START_ADDON_PARSER;
    if (!lexer_push_input(path))
        return FALSE;

    CGRP_SET_FLAG(ctx->options.flags, CGRP_FLAG_ADDON_RULES);
    success = cgrpyyparse(ctx) == 0;
    CGRP_CLR_FLAG(ctx->options.flags, CGRP_FLAG_ADDON_RULES);

    return success;
}


/********************
 * config_dump
 ********************/
void
config_print(cgrp_context_t *ctx, FILE *fp)
{
    partition_dump(ctx, fp);
    group_dump(ctx, fp);
    procdef_dump(ctx, fp);
}


void
cgrpyyerror(cgrp_context_t *ctx, const char *msg)
{
    (void)ctx;

    OHM_ERROR("parse error: %s near line %d in file %s", msg,
              lexer_line(), lexer_file());
#if 0
    exit(1);                              /* XXX would be better not to */
#endif
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
