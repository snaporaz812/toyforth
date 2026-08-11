#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>

// ============== DATA STRUCTURES ==============

#define TFOBJ_TYPE_INT 0
#define TFOBJ_TYPE_STRING 1
#define TFOBJ_TYPE_BOOL 2
#define TFOBJ_TYPE_LIST 3
#define TFOBJ_TYPE_SYMBOL 4


typedef struct tfobj{
	int refcount;
	int type; // TFOBJ_TYPE_*

	// Position in the file
	int line, line_offset;

	union {
		// INT and BOOL
		int i;

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
	int line, line_offset;	
	char *prg;	// <-- The program to compile into a list
	char *p;	// <-- Next token to parse
} tfparser;

// Usage context
typedef struct tfctx{ 
	tfobj *stack; // It is goint to be used as a LIST
} tfctx;


// ============== ALLOCATION WRAPPERS ==============

// Try to allocate n bytes, else close the program
void *xmalloc(size_t size) {
	void *ptr = malloc(size);
	if (ptr == NULL) {
		fprintf(stderr, "Out of memory allocating %zu bytes\n", size);
		exit(1);
	}
	return ptr;
}

// ============== OBJECT-RELATED FUNCTIONS ==============

/*Allocate and initialize ToyForth Objects of the required types*/

tfobj *create_object(int type){
	tfobj *o = xmalloc(sizeof(tfobj));
	o->type = type;
	o->refcount = 1;
	/*
	o->line = 0;
	o->line_offset = 0;
	*/
	return o;
};


void destroy_tfobject(tfobj *o) {
	if (o->refcount > 1) {
		fprintf(stderr, "Object still has references\n");
		exit(1);

	} else if (o->refcount < 1) {
		fprintf(stderr, "An anomaly with the references of the object occurred");
		exit(1);

	} else free(o); // refcount == 0 --> tfobj gets destroyed
};


tfobj *create_string_object(char *s, size_t len) {
	tfobj *o = create_object(TFOBJ_TYPE_STRING);
	o->str.ptr = s;
	o->str.len = len;
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
	tfobj *o = create_object(TFOBJ_TYPE_SYMBOL);
	o->str.ptr = s;
	o->str.len = len;
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

// Increase reference count of the specified Toyforth object
void retain(tfobj *o) {
	o->refcount++;
};

// Decrease reference count of the specified Toyforth object
void release(tfobj *o) {
	o->refcount--;
	if (o->refcount == 0) destroy_tfobject(o);
};

// ================= LIST OBJECT FUNCTIONS =======================

tfobj *create_list_object(void) {
	tfobj *o = create_object(TFOBJ_TYPE_LIST);
	o->list.ele = NULL;
	o->list.len = 0;
	return o;
};


/* Append a new element to the end of a list,
 * The caller should then increase the reference count manually.
*/
void append(tfobj *l, tfobj *ele) {
	// Reallocate the memory so that it can contain the elements of the array 
	tfobj **new_ele = realloc(l->list.ele, sizeof(tfobj *) * (l->list.len + 1));
    if (!new_ele) {
        // Handle memory allocation failure appropriately
		fprintf(stderr, "Error appending an element to a list");
        goto safelander; 
    }

    l->list.ele = new_ele;

	// Append the element at the last position of the list
	l->list.ele[l->list.len] = ele;

	// Increase refcount and list length
	retain(ele);
	l->list.len++;

	safelander:
};

// ============= EXECUTION CONTEXT ================

tfctx *create_context() {
	tfctx *ctx = xmalloc(sizeof(*ctx));
	ctx->stack = create_list_object();
	return ctx;
};

// ============= COMPILING AND PARSING =============

void incr_parser_token(tfparser *parser) {
	parser->p++;
	parser->line_offset++;
}

void skip_trivia(tfparser *parser) {
	int debug = 0;
	if (debug==1) printf("##### SKIP TRIVIA #####\n");
	
	// Skip spaces
	while (isspace(parser->p[0])) {incr_parser_token(parser);}

	if (debug==1) printf("line: %d, offset: %d\n",parser->line, parser->line_offset);
	
	// Skip comments (marked by # and ending with '\n') 
	if (parser->p[0] == '#') {
		incr_parser_token(parser);
		while (parser->p[0] != '\n') {incr_parser_token(parser);}
	}

	// Skip newlines 
	if (parser->p[0] == '\n') {
		incr_parser_token(parser);
		parser->line++;
		parser->line_offset = 1;
	}
	if (debug==1) printf("##### /SKIP TRIVIA #####\n");
};



#define MAX_NUM_LEN 128
// This function accepts a parser and returns a ToyForth int object containing the parsed integer numbers
tfobj *parse_numbers(tfparser *parser) {
	char buf[MAX_NUM_LEN];
	char *start = parser->p; // Record the start of the number (either number or sign)
	char *end;
	tfobj *o = NULL;

	/* This check should be useless IN THEORY, as it is already evaluated in compile()
	 * before parse_numbers() is called
	 */

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
	 *	atoi() "casts" an ASCII string to int 
	 */
	o = create_int_object(atoi(buf));
	o->line_offset = parser->line_offset;

	return o; 
};

#define MAX_STRING_LEN 512 
#define STRING_CHAR 34 // ASCII 34 = "
tfobj *parse_strings(tfparser *parser) {

	char buf[MAX_STRING_LEN];
	char *start = parser->p;
	char *end;
	size_t len;
	tfobj *o = NULL;
	char *string_marker = (char*)STRING_CHAR;

	// --------------------
	/* Non sono sicuro di come funziona questo blocco.
	 * Non so se conviene comparare carattere per carattere con parser->p[0]
	 * o se comparare a stringhe parser->p
	 * (ammesso e non concesso che sia davvero così)
	 */
	// Check: "...
	if (strcmp(start, string_marker) || !(strcmp(parser->p[0], string_marker))) {
		parser->p++;
	
	// Check: "..."
	} else if (strcmp(start, string_marker) && (strcmp(parser->p[0], string_marker))) {
		end = parser->p[0];
		parser->p++;
	
	// Check: ...
	} else return o;
	// --------------------
	
	len = end-start;

	if (len >= MAX_STRING_LEN) return o;

	/* Cut off the "" markers
	 * and reduce the needed allocated space to the bare minimum
	 */
	// NEEDED MEMORY SPACE = MAX_STRING_LEN-(MAX_STRING_LEN-x)
	memcpy(buf, start+1, MAX_STRING_LEN-(MAX_STRING_LEN-len)-1);
	buf[len] = 0;

	o = create_string_object(buf, len);

	return o;
};


#define MAX_KEYWORD_LEN 6 // Symbols can be of max 5 characters
int issymbol(char *word, size_t len) {

	// --- INITIALIZE KEYWORDS AND SYMBOLS ---
	const char *keywords[] = {
	// len = 4
	"dup",		// duplicate
	"add",		// add
	"sub",		// subtract
	"mul",		// multiply
	"div",		// divide
	"mod",		// modulo
	"var",		// variable
	"del",		// delete

	// len = 5
	"func",		// function

	// len = 6
	"print",	// print
	"reset"		// reset
	};

	const char *symbols[] = {"+*-/()[]{}"};
	// ---------------------------------------

	for (size_t i=0; i < len; i++) {

		// Check if WORD is a symbol
		if (word[i] == symbols[i]) {return 0;}

		// Check if WORD is a keyword
		else if (word == keywords[i]) {return 0;}

		else return 1;
	}

	/*
	return 1 // false
	return 0 // true
	*/
};

tfobj *parse_symbol(tfobj *parser) {
	
	char keyword;
	size_t i;

	tfobj *o = NULL;


	o = create_symbol_object(keyword, i);

	return o;
};

// Translate .txt content into tf objects 
tfobj *compile(char *prg) {
	tfparser *parser = create_tfparser(prg);
	tfobj *parsed = create_list_object();
	tfobj *o = NULL;

	// This cycle goes on as long as there are elements in the array parser->p[]
	while (parser->p) {
		char *parse_token_start = parser->p;
		
		skip_trivia(parser);

		if (parser->p[0] == 0) break; // End of Program reached

		// Parse symbols

		// Parse numbers (either signed or unsigned)
		if (isdigit(parser->p[0]) ||
			((parser->p[0] == '+' || parser->p[0] == '-') && isdigit(parser->p[1]))) {o = parse_numbers(parser);}
		else o = NULL;

		// Parse booleans

		// check for errors when token was being parsed
		if (o == NULL) {
			printf("---!!!---\n");
			fprintf(stderr, "Syntax error at line %d, col %d: \n|%16s|\n", parser->line, parser->line_offset, parse_token_start);
			printf("---!!!---\n");
			//return NULL;
			exit(1);
		} else {
			append(parsed, o);
		}
		
	}
	return parsed;
};


void exec(tfobj *prg) {
	//Initialize stack instance
	tfctx *ctx = create_context(); //


	while (ctx->stack->list.ele) {
		switch (prg->type) {

		// --- Object must be appended to latest position in stack
		case TFOBJ_TYPE_BOOL:
		case TFOBJ_TYPE_INT:
			append(ctx->stack, prg->i);
			break;

		case TFOBJ_TYPE_STRING:
			append(ctx->stack, prg->list.ele);
			break;

		case TFOBJ_TYPE_SYMBOL:
			// 
			break;

		case TFOBJ_TYPE_LIST:
			// manage functions (which are lists of operations)
			exec(prg->list.ele); // recursive
			break;

		default:
			fprintf(stderr, "Runtime error");
		}
	}
};

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


// ============== MAIN ==============

int main(int argc, char **argv) {

	if (argc < 2) {fprintf(stderr, "Usage: %s <filename>\n", *argv); return 1;}

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

	printf("---\n");
	printf("Program text: |%s|\n", prgtext);
	printf("---\n");
	

	// --------
	tfobj *prg = compile(prgtext);

	print_prg(prg);

	return 0;
}

/* ======= TO DO LIST: ==========
 * finire il compiler (compile())

 * per finire il compiler serve finire compile() --> (aggiungere i bool e le altre cose)

 * Creare exec(). Deve prendere in input una lista contenente degli oggetti, checkare iterativamente
 * l'oggetto in questione, e in base a quello aggiungerlo alla stack o fare operazioni con oggetti già nella stack 
*/

/*
Mi sa che droppo perché non ci sto più capendo un cazzo
*/