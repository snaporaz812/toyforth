#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>



// ============== DATA STRUCTURES ==============

#define TF_OK 0
#define TF_ERR 1

#define TFOBJ_TYPE_INT 0
#define TFOBJ_TYPE_STRING 1
#define TFOBJ_TYPE_BOOL 2
#define TFOBJ_TYPE_LIST 3
#define TFOBJ_TYPE_SYMBOL 4
#define TFOBJ_TYPE_ALL 255 // Used by list_pop() and other functions.


typedef struct tfobj{
	int refcount;
	int type; // TFOBJ_TYPE_*

	union {
		// INT and BOOL
		int i; // FIXME: ma non è che dovrei farlo signed int forse???
		//int b // to store only boolean values

		// STRING and SYMBOL
		struct {
			char *ptr;
			size_t len;
		} str;

		// LIST
		struct { 
			struct tfobj **ele; //list.ele is the array that contains list.ele[0], list.ele[1], etc
			size_t len;
		} list;
	};
} tfobj;

typedef struct tfparser {
	int line, line_offset;	// Position in the instruction file
	char *prg;	// <-- The program to compile into a list
	char *p;	// <-- Next token to parse
} tfparser;

struct tfctx;

/* Function table entry: each of these entries represents a symbol name
 * associated with a function implementation. */
typedef struct FunctionTableEntry {
	tfobj *name; // TFOBJ_TYPE_SYMBOL
	int (*callback) (struct tfctx *ctx, char *name); // Callback in C
	tfobj *user_func; // TFOBJ_TYPE_LIST
} tffunctabentry; 


struct FunctionTable {
	tffunctabentry **func_table;
	size_t func_count;
};

// Execution context
typedef struct tfctx{ 
	tfobj *stack; // It is goint to be used as a LIST
	struct FunctionTable functable;
} tfctx;

// =========================== FUNCTION PROTOTYPES ===============================

//int compare_string_objects(tfobj *a, tfobj *b);

tffunctabentry *get_function_by_name(tfctx *ctx, tfobj *name);

int ctx_check_stack_min_len(tfctx *ctx, size_t min);
tfobj *ctx_stack_pop(tfctx *ctx, int type);
void ctx_stack_push(tfctx *ctx, tfobj *o);

 // ===============================================================================

// ============== ALLOCATION WRAPPERS ==============

// Try to allocate n bytes, else close the program.
void *xmalloc(size_t size) {
	void *ptr = malloc(size);
	if (ptr == NULL) {
		fprintf(stderr, "Out of memory allocating %zu bytes\n", size);
		exit(1);
	}
	return ptr;
}

// Try to reallocate n bytes, else close the program.
void *xrealloc(void *oldptr, size_t size) {
	void *ptr = realloc(oldptr, size);
	if (ptr == NULL) {
		fprintf(stderr, "Out of memory reallocating %zu bytes\n", size);
		exit(1);
	}
	return ptr;
};

// ============== OBJECT-RELATED FUNCTIONS ==============

/*Allocate and initialize ToyForth Objects of the required types*/

tfobj *create_object(int type){
	tfobj *o = xmalloc(sizeof(tfobj));
	o->type = type;
	o->refcount = 1;
	return o;
};

// Increase reference count of the specified TFObject
void retain(tfobj *o) {
	o->refcount++;
};

void free_tfobject(tfobj *o); // Forward declaration

// Decrease reference count of the specified TFObject
void release(tfobj *o) {
	if (!o) return;
	assert(o->refcount > 0);

	o->refcount--;
	if (o->refcount == 0) free_tfobject(o);
};

// Destroy a TFObject without checking its reference count.
void free_tfobject(tfobj *o) {
	assert(!(o->refcount < 0)); // check if refcount isn't negative
	
	/*if (o->refcount > 0) {
		fprintf(stderr, "Object still has references\n");
		exit(1);

	// refcount == 0 --> tfobj gets destroyed
	} else {*/
		switch (o->type) {
		case TFOBJ_TYPE_LIST:
			for (size_t i=0; i < o->list.len; i++) {
				release(o->list.ele[i]); // Release child elements
			}
			free(o->list.ele); // Release array
			break;

		case TFOBJ_TYPE_SYMBOL:
		case TFOBJ_TYPE_STRING:
			free(o->str.ptr);
			break;
		}

		free(o);
	//}; 
};

// Initialize string and symbol type TFObjects' fundamentals
tfobj *create_text_object(char *s, size_t len, int type) {
	tfobj *o = create_object(type);
	o->str.ptr = xmalloc(len+1);
	memcpy(o->str.ptr, s, len);
	o->str.ptr[len] = 0;
	o->str.len = len;

	return o;	
};

tfobj *create_string_object(char *s, size_t len) {
	tfobj *o = create_text_object(s, len, TFOBJ_TYPE_STRING);
	return o;
};

tfobj *create_int_object(int i) {
	tfobj *o = create_object(TFOBJ_TYPE_INT);
	o->i = i;
	return o;
};

tfobj *create_bool_object(int i){
	tfobj *o = create_object(TFOBJ_TYPE_BOOL);
	o->i = i;
	return o;
};

tfobj *create_symbol_object(char *s, size_t len) {
	tfobj *o = create_text_object(s, len, TFOBJ_TYPE_SYMBOL);
	return o;
};

tfparser *create_tfparser(char *prgtext) {
	tfparser *o = xmalloc(sizeof(tfparser));
	o->prg = prgtext; // prgtext is set as current string
	o->p = prgtext; // prgtext is set as next string to be read
	o->line = 1;
	o->line_offset = 1;
	return o;
};

/* Print the the contents of a compiled program. */
void print_prg(tfobj *prg) {
	printf("[");

	for (size_t i=0; i < prg->list.len; i++) {
		tfobj *ele = prg->list.ele[i]; 
		switch (ele->type) {
		case TFOBJ_TYPE_INT:
		case TFOBJ_TYPE_BOOL:
			printf("%d", ele->i);
			break;

		case TFOBJ_TYPE_STRING:
			printf("\"%s\"", ele->str.ptr);
			break;

		case TFOBJ_TYPE_SYMBOL:
			printf("%s", ele->str.ptr); 
			break;

		case TFOBJ_TYPE_LIST:
			print_prg(ele); // Recursive call
		}
		if (i != prg->list.len -1) printf(" ");
	}
	printf("]\n");
};

int compare_string_objects(tfobj *a, tfobj *b) {
	//if (a->type != b->type) return TF_ERR;

	int result = strcmp(a->str.ptr, b->str.ptr);
	if (result != 0) {
		result = TF_ERR;
	} else result =  TF_OK;

	return result;
	
};


// ================= LIST OBJECT FUNCTIONS =======================

tfobj *create_list_object(void) {
	tfobj *o = create_object(TFOBJ_TYPE_LIST);
	o->list.ele = NULL;
	o->list.len = 0;
	return o;
};


/* Append a new element to the end of a list.
 * Increasing the refcount is up to the caller. */
void append(tfobj *l, tfobj *ele) {
	// Reallocate the memory so that the array can contain one more element 
	tfobj **new_ele = xrealloc(l->list.ele, sizeof(tfobj *) * (l->list.len + 1));
    
	// Handle memory allocation failure appropriately
	if (!new_ele) {
		fprintf(stderr, "Error appending an element to a list");
        exit(1);
    }

    l->list.ele = new_ele;

	// Append the element at the last position of the list
	l->list.ele[l->list.len] = ele;

	l->list.len++;
};


/* Pop the top element from the interpreter's main stack, assuming it
 * will match "type", otherwise NULL is returned, Also, the function
 * returns NULL if the stack is empty.
 * 
 * The reference counting of the popped object is not modified. It is
 * assumed that we just transfer the ownership from the stack to the caller. */
tfobj *list_pop_type(tfctx *ctx, int type) {
	tfobj *stack = ctx->stack;
	if (stack->list.len == 0) return NULL;

	tfobj *to_pop = stack->list.ele[stack->list.len -1];
	if (type != TFOBJ_TYPE_ALL && to_pop->type != type) return NULL;
	
	//
	stack->list.len--;
	if (stack->list.len == 0) {
		free(stack->list.ele);
		stack->list.ele = NULL;
	} else {
		stack->list.ele = xrealloc(stack->list.ele,
										sizeof(tfobj*) * (stack->list.len));
	}
	return to_pop;
};

tfobj *list_pop(tfctx *ctx) {
	return list_pop_type(ctx, TFOBJ_TYPE_ALL);
};

// ========================= FUNCTIONS TO MANAGE TF FUNCTIONS ==================================


/* Push a new function entry in the context. It's up to the caller
 * to set either the C callback or the list representing the
 * user-defined functions. */
tffunctabentry *register_function(tfctx *ctx, tfobj *name)
{
	ctx->functable.func_table = xrealloc(ctx->functable.func_table,
										 sizeof(tffunctabentry*) * (ctx->functable.func_count+1));
	tffunctabentry *fe = xmalloc(sizeof(tffunctabentry));
	ctx->functable.func_table[ctx->functable.func_count] = fe;
	ctx->functable.func_count++;
	fe->name = name;
	retain(name);
	fe->callback = NULL;
	fe->user_func = NULL;

	return fe;
}

/* Register a new function with the given name in the function table
 * of the context. The function cannot fail, since if a function with the
 * same name already exists, it gets replaced by the new one. */
void register_c_function(tfctx *ctx, char *name,
						 int (*callback) (tfctx *ctx, char *name))
{
	tffunctabentry *fe;
	tfobj *oname = create_string_object(name, strlen(name));

	fe = get_function_by_name(ctx, oname);
	if (fe) {
		if (fe->user_func) {
			release(fe->user_func);
			fe->user_func = NULL;
		}
		fe->callback = callback;
	} else {
		fe = register_function(ctx, oname);
		fe->callback = callback;
	}
	release(oname);
};


/* Resolve the function by scanning the function table and by looking for a matching symbol name.
 * If a matching function was not found, NULL will be returned.
 * Otherwise, the function returns the function entry object. */
tffunctabentry *get_function_by_name(tfctx *ctx, tfobj *name) {
	for (size_t i=0; i < ctx->functable.func_count; i++) {
		tffunctabentry *fe = ctx->functable.func_table[i];
		if (compare_string_objects(fe->name, name) == TF_OK) return fe;
	}
	return NULL;
};

/* Try to call the function associated with the symbol name "sym".
 * Return 0 if the symbol name is actually bound to a function,
 * return 1 otherwise.*/
int call_symbol(tfctx *ctx, tfobj *sym) {
	tffunctabentry *fe = get_function_by_name(ctx, sym);

	if (fe == NULL) {
		return TF_ERR;
    } else if (fe->user_func) {
		//TODO
		return TF_ERR;
	} else return fe->callback(ctx, fe->name->str.ptr);
};


// ============= COMPILING AND PARSING =============

// Increase parser's p (aka the next token) and line_offset
void incr_parser_token(tfparser *parser) {
	parser->p++;
	parser->line_offset++;
}

// Skip whitespaces and comments (#...\n)
void skip_trivia(tfparser *parser) {
	while (1) {
		// Skip whitespaces
		while (parser->p[0] && isspace((unsigned char)parser->p[0])) {
			// Manage newlines correctly
			if (parser->p[0] == '\n') {
				parser->p++;
				parser->line_offset = 1;
				parser->line++;
			} else incr_parser_token(parser);
		}

		// Skip comments
		if (parser->p[0] == '#') {
			while (parser->p[0] && parser->p[0] != '\n') {incr_parser_token(parser);}
			continue;
		}

		break;
	}
};

#define MAX_NUM_LEN 128
// This function accepts a parser and returns a ToyForth int object containing the parsed integer numbers
tfobj *parse_numbers(tfparser *parser) {
	char buf[MAX_NUM_LEN];
	char *start = parser->p; // Record the start of the number (either number or sign)
	char *end;
	tfobj *o = NULL;

	/* This check should be useless IN THEORY, as it is already evaluated in compile()
	 * before parse_numbers() is called. */

	// Check whether the sign is followed by a number
	if (((parser->p[0] == '+') || (parser->p[0] == '-')) && isdigit(parser->p[1])) {incr_parser_token(parser);}
	else if (!isdigit(parser->p[0])) {return o;} // Return NULL object which will throw a segmentation fault.

	// Get to the end of the number and record its index
	while (parser->p[0] && isdigit(parser->p[0])) {incr_parser_token(parser);}
	end = parser->p;
	int numlen = end-start;

	// Error if number is too big
	if (numlen >= MAX_NUM_LEN) {return o;}

	// Copy parsed content to buffer, then set null term at the end of the array 
	memcpy(buf, start, numlen);
	buf[numlen] = 0;

	/* Create a tfobject containing the parsed number as an integer.
	 *	atoi() "casts" an ASCII string to int. */
	o = create_int_object(atoi(buf));

	return o; 
};

/*
#define MAX_BOOL_LEN 4 // sizeof(int) = 4
// This function accepts a parser and returns a ToyForth int object containing the parsed boolean value
tfobj *parse_booleans(tfparser *parser) {
	char buf[MAX_BOOL_LEN];
	char *start = parser->p; // Record the start of the number (either number or sign)
	tfobj *o = NULL;

	// Check whether the the number is either 0 or 1
	if ((parser->p[0] == 0) || (parser->p[0] == 1)) {incr_parser_token(parser);}

	// Otherwise return a NULL object which will throw a segmentation fault.
	else if ((!isdigit(parser->p[0])) || (parser->p[0]) != 0) || (parser->p[0]) != 1)) {
		return o;
	}

	// Get to the end of the number and record its index
	while (parser->p[0] && isdigit(parser->p[0])) {incr_parser_token(parser);}
	end = parser->p;
	int numlen = parser->p - start;

	// Error if number is too big
	if (numlen >= MAX_BOOL_LEN) {return o;}

	// Copy parsed content to buffer, then set null term at the end of the array 
	memcpy(buf, start, numlen);
	buf[numlen] = 0;

	// Create a tfobject containing the parsed number as an integer.
	//	atoi() "casts" an ASCII string to int.
	o = create_int_object(atoi(buf));

	return o; 
};*/

#define MAX_STRING_LEN 512 
#define STRING_CHAR 34 // ASCII 34 = "
/* This function accepts a parser and returns a ToyForth string object
 * which contains the parsed pointer to the string and the string's length. */
tfobj *parse_strings(tfparser *parser) {

	char buf[MAX_STRING_LEN];
	char *start = parser->p;
	size_t len;
	tfobj *o = NULL;
	// define the charachter that will be checked for as the start of a string: " 
	char string_marker = (char)STRING_CHAR;

	// Check for: " 
	if (*start == string_marker) {
		incr_parser_token(parser);

		while (parser->p[0] != string_marker) {
			incr_parser_token(parser);
		}
		incr_parser_token(parser); // Skip closing quotation mark 
	} else return o;
	
	len = (parser->p - 1) - (start + 1); // Don't count opening quotation mark
	if (len+1 >= MAX_STRING_LEN) return o; // String is too big

	// Allocate the needed space (= string+'\0')
	memcpy(buf, start+1, len+1); // Cut off opening quotation mark
	buf[len] = 0;

	o = create_string_object(buf, len);

	return o;
};

#define MAX_KEYWORD_LEN 5 // Symbols can be max 5 characters long
/* This function returns 0 if the input string corresponds to a set of chosen keywords and symbols,
 * returns 1 otherwise. */
int issymbol(char *word) {

	// --- INITIALIZE KEYWORDS AND SYMBOLS ---
	const char *keywords[] = {
	// len = 3
	"dup",		// duplicate
	"add",		// add
	"sub",		// subtract
	"mul",		// multiply
	"div",		// divide
	"mod",		// modulo
	"var",		// variable
	"del",		// delete

	// len = 4
	"func",		// function

	// len = 5
	"print",	// print
	"reset",	// reset
	NULL
	};

	const char symbols[] = {"+*-/()[]{}"};
	// ---------------------------------------
	
	size_t i = 0;
	// Check punctuation
	if (ispunct(word[0])) {
		while (symbols[i]) {
			if (strchr(word, (int)symbols[i])) return TF_OK;
			i++;
		}

	// Check keyword 
	} else if (isalpha(word[0])) {
		while (keywords[i]) {
			if (strcmp(word, keywords[i]) == 0) return TF_OK;
			i++;
		}
	}
	
	return TF_ERR;
};

/* This function accepts a parser and returns a ToyForth symbol object
 * which contains the parsed pointer to the string and the string's length. */
tfobj *parse_symbols(tfparser *parser) {
	
	char buf[MAX_KEYWORD_LEN+1];
	char *start = parser->p;
	size_t len;
	tfobj *o = NULL;

	// Extract the string to be evaluated
	while (isalpha(parser->p[0]) || ispunct(parser->p[0])) incr_parser_token(parser);
	len = parser->p - start;
	if (len > MAX_KEYWORD_LEN+1) return o; // Keyword is too big 

	memcpy(buf, start, len+1); // allocate space for string + null term
	buf[len] = 0;

	// Check if string is a SYMBOL
	if (issymbol(buf) == 0) o = create_symbol_object(buf, len);

	return o;
};

// Translate instructions into TFObjects 
tfobj *compile(char *prg) {
	tfparser *parser = create_tfparser(prg);
	tfobj *parsed = create_list_object();

	// This cycle goes on as long as there are elements in the array parser->p[]
	while (parser->p) {
		tfobj *o = NULL;
		char *parse_token_start = parser->p;
		
		skip_trivia(parser);

		if (parser->p[0] == 0) break; // End of Program reached

		// Parse strings if quotation mark (") is found
		if (parser->p[0] == (char)STRING_CHAR) o = parse_strings(parser);

		// Parse numbers (either signed or unsigned)
		if (isdigit(parser->p[0]) ||
			((parser->p[0] == '+' || parser->p[0] == '-') && isdigit(parser->p[1]))) o = parse_numbers(parser);

		// Parse booleans

		// Parse symbols
		if (isalpha(parser->p[0]) || ispunct(parser->p[0])) o = parse_symbols(parser); 
		
		
		// Check for errors when token was being parsed
		if (o == NULL) {
			release(parsed);

			printf("\t---!!!---\n");
			fprintf(stderr, "\tSyntax error at line %d, col %d: \n|%8s|\n", parser->line, parser->line_offset, parse_token_start);
			printf("\t---!!!---\n");
			//return NULL;
			exit(1);
		} else {
			append(parsed, o);
		}
		
	}
	free(parser);
	return parsed;
};

// ============================== Basic Standard Library ======================================

int basic_math_functions(tfctx *ctx, char *name) {
	if (ctx_check_stack_min_len(ctx, 2) == TF_ERR) return TF_ERR;
	
	tfobj *b = ctx_stack_pop(ctx, TFOBJ_TYPE_INT);
	if (b == NULL) return TF_ERR;
	tfobj *a = ctx_stack_pop(ctx, TFOBJ_TYPE_INT);
	if (a == NULL) {
		ctx_stack_push(ctx, b);
		return TF_ERR;
	}
	if (a == NULL || b == NULL) return TF_ERR;

	int result;
	switch(name[0]) {
	case '+':
		result = a->i + b->i;
		break;
	case '-':
		result = a->i - b->i;
		break;
	case '*':
		result = a->i * b->i;
		break;
	case '/':
		result = a->i / b->i;
		break;
	case '%':
		result = a->i % b->i;
		break;
	}

	release(a);
	release(b);
	
	tfobj *oresult = create_int_object(result);
	ctx_stack_push(ctx, oresult);

	return TF_OK;
	
};

// =================================== EXECUTION & CONTEXT ==================================================

/* This function evaluates whether the stack's len is less than provided minimum input.
 * If the stack exceeds the minimum number. 0 (true) is returned. Else 1 is returned*/
int ctx_check_stack_min_len(tfctx *ctx, size_t min) {
	return ctx->stack->list.len < min ? TF_ERR : TF_OK;
};

/* Pop the top element from the interpreter's main stack, assuming it
 * will match "type", otherwise NULL is returned, Also, the function
 * returns NULL if the stack is empty.
 * 
 * The reference counting of the popped object is not modified. It is
 * assumed that we just transfer the ownership from the stack to the caller. */
tfobj *ctx_stack_pop(tfctx *ctx, int type) {
	return list_pop_type(ctx, type);
};


/* Just push the object on the interpreter's main stack. */
void ctx_stack_push(tfctx *ctx, tfobj *o) {
	append(ctx->stack, o);
};

tfctx *create_context(void) {
	tfctx *ctx = xmalloc(sizeof(*ctx));
	ctx->stack = create_list_object();
	ctx->functable.func_table = NULL;
	ctx->functable.func_count = 0;
	register_c_function(ctx, "+", basic_math_functions);
	return ctx;
};

void free_tfcontext(tfctx *ctx) {
	assert(ctx->stack->refcount > 0);

	free_tfobject(ctx->stack);
	free(ctx);
};

/* Execute the instructions stored in the compiled program
 * by appending them onto the stack. */
int exec(tfctx *ctx, tfobj *prg) {
	assert(prg->type == TFOBJ_TYPE_LIST);

	// Append objects to stack
	for (size_t i=0; i<prg->list.len; i++) {
		tfobj *word = prg->list.ele[i];

		switch (word->type) {
		case TFOBJ_TYPE_SYMBOL:
			if (call_symbol(ctx, word) == TF_ERR) {
				printf("Runtime error\n");
				return TF_ERR;
			};
			break;
		
		/*
		case TFOBJ_TYPE_LIST:
			// manage functions (which are lists of operations)
			for (size_t j=0; j < prg->list.len; j++) exec(ctx, prg->list.ele[j]); // recursive
			break;*/

		default:
			ctx_stack_push(ctx, word);
			retain(word);
			break;
		}
	}
	return TF_OK;
};

// ============== MAIN ==============

int main(int argc, char **argv) {

	// ========== READ FROM FILE ==============
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <filename>\n", *argv);
		return 1;
	}

	// Read the program in memory, for later parsing
	FILE *fp = fopen(argv[1], "r");
	if (fp == NULL) {
		fprintf(stderr, "Error reading toyforth program\n");
		return 1;
	}

	fseek(fp, 0, SEEK_END);
	long filesize = ftell(fp);
	printf("Source file size: %ld\n", filesize);

	// Allocate the program content in the memory
	char *prgtext = malloc(filesize+1);

	fseek(fp, 0, SEEK_SET);
	fread(prgtext, filesize, 1, fp); 
	prgtext[filesize] = 0; // Add null term to the end of the array
	fclose(fp);

	printf("---\nProgram text:\n|%s|\n---\n\n", prgtext);
	
	// =============== COMPILE AND EXECUTE ===================
	tfobj *prg = compile(prgtext);

	printf("Compiled program content: "); // temp
	print_prg(prg); // temp


	//Initialize stack instance
	tfctx *ctx = create_context();
	exec(ctx, prg);

	printf("Stack content: "); // temp
	print_prg(ctx->stack); // temp

	// ----- Terminating sequence -----
	release(prg); // dà errori perché devo fare la release() di degli oggetti dentro la stack
	free_tfcontext(ctx);

	return 0;
}

/* ======= TO DO LIST: ==========
 * FUNZIONI: https://youtu.be/oMj3N6jYIUU?si=AFKei0RFCaOdVszr&t=1974
 */