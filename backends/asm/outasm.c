//
// Pasm data output for spin2cpp
//
// Copyright 2016 Total Spectrum Software Inc.
// see the file COPYING for conditions of redistribution
//
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include "spinc.h"
#include "ir.h"

/* define this to pass parameters in ARGSx_ */
#define USE_GLOBAL_ARGS

static Operand *mulfunc, *mula, *mulb;
static Operand *divfunc, *diva, *divb;

static Operand *nextlabel;
static Operand *quitlabel;

static Operand *datbase;
static Operand *datlabel;

typedef struct OperandList {
  struct OperandList *next;
  Operand *op;
} OperandList;

static Operand *CompileExpression(IRList *irl, AST *expr);
static Operand* CompileMul(IRList *irl, AST *expr, int gethi);
static Operand* CompileDiv(IRList *irl, AST *expr, int getmod);
static Operand *Dereference(IRList *irl, Operand *op);
static Operand *CompileFunccall(IRList *irl, AST *expr);
static Operand *CompileIdentifierForFunc(IRList *irl, AST *expr, Function *func);

static void EmitGlobals(IRList *irl);
static void EmitMove(IRList *irl, Operand *dst, Operand *src);
static void EmitBuiltins(IRList *irl);
static IR *EmitOp1(IRList *irl, Operandkind code, Operand *op);
static IR *EmitOp2(IRList *irl, Operandkind code, Operand *op, Operand *op2);
static void CompileConsts(IRList *irl, AST *consts);
static void EmitAddSub(IRList *irl, Operand *dst, int off);

typedef struct AsmVariable {
    Operand *op;
    intptr_t val;
} AsmVariable;

// global variables in COG memory (really holds struct AsmVariables)
static struct flexbuf cogGlobalVars;

// global variables in hub memory (really holds struct AsmVariables)
static struct flexbuf hubGlobalVars;

// returns true if P is the top level module for this project
static int
IsTopLevel(Module *P)
{
    return 1; // FIXME not accurate yet
}

static const char *
IdentifierLocalName(Function *func, const char *name)
{
    char temp[1024];
    Module *P = func->parse;

    if (IsTopLevel(P)) {
        snprintf(temp, sizeof(temp)-1, "%s_%s_", func->name, name);
    } else {
        snprintf(temp, sizeof(temp)-1, "%s_%s_%s_", P->basename, func->name, name);
    }
    return strdup(temp);
}

static const char *
IdentifierGlobalName(Module *P, const char *name)
{
    char temp[1024];
    if (IsTopLevel(P)) {
        return name;
    } else {
        snprintf(temp, sizeof(temp)-1, "%s_%s", P->basename, name);
        return strdup(temp);
    }
}

static int IsMemRef(Operand *op)
{
    return op && (op->kind >= LONG_REF) && (op->kind <= BYTE_REF);
}

static Operand *
GetVar(struct flexbuf *fb, Operandkind kind, const char *name, intptr_t value)
{
  size_t siz = flexbuf_curlen(fb) / sizeof(AsmVariable);
  size_t i;
  AsmVariable tmp;
  AsmVariable *g = (AsmVariable *)flexbuf_peek(fb);
  for (i = 0; i < siz; i++) {
    if (strcmp(name, g[i].op->name) == 0) {
      return g[i].op;
    }
  }
  tmp.op = NewOperand(kind, name, value);
  tmp.val = value;
  flexbuf_addmem(fb, (const char *)&tmp, sizeof(tmp));
  return tmp.op;
}

Operand *GetGlobal(Operandkind kind, const char *name, intptr_t value)
{
    return GetVar(&cogGlobalVars, kind, name, value);
}
Operand *GetHub(Operandkind kind, const char *name, intptr_t value)
{
    return GetVar(&hubGlobalVars, kind, name, value);
}

IR *NewIR(enum IROpcode kind)
{
    IR *ir = malloc(sizeof(*ir));
    memset(ir, 0, sizeof(*ir));
    ir->opc = kind;
    return ir;
}

IR *DupIR(IR *old)
{
    IR *ir = NewIR(0);
    memcpy(ir, old, sizeof(*ir));
    ir->prev = ir->next = NULL;
    return ir;
}

//
// replace the individual IR "ir" in list "irl" with a list of
// instructions from function f; used for e.g. expanding inline functions
//
void ReplaceIRWithInline(IRList *irl, IR *ir, Function *func)
{
    IR *newir;
    IR *dest = ir->prev;
    IRList *insert = FuncIRL(func);
    
    DeleteIR(irl, ir);
    ir = insert->head;
    while (ir) {
        newir = DupIR(ir);
        // leave off the asm return name, if it's there
        // FIXME: this is probably an obsolete test now
        if (newir->opc == OPC_LABEL && newir->dst == FuncData(func)->asmretname) {
            /* do nothing */
        } else {
            InsertAfterIR(irl, dest, newir);
            dest = newir;
        }
        ir = ir->next;
    }
}

Operand *NewOperand(enum Operandkind k, const char *name, int value)
{
    Operand *R = malloc(sizeof(*R));
    memset(R, 0, sizeof(*R));
    R->kind = k;
    R->name = name;
    R->val = value;
    if (k == IMM_COG_LABEL || k == IMM_HUB_LABEL) {
      R->used = 1;
    }
    return R;
}

/*
 * append some IR to the end of a list
 */
void
AppendIR(IRList *irl, IR *ir)
{
    IR *last = irl->tail;
    InsertAfterIR(irl, last, ir);
}

/*
 * insert a new IR element after element orig
 * if orig == NULL then place orig in the
 * beginning of the list
 */

void
InsertAfterIR(IRList *irl, IR *orig, IR *ir)
{
    IR *o_next;

    if (!ir) return;
    
    if (!orig) {
        /* place at the beginning of the list */
        o_next = irl->head;
        irl->head = ir;
        ir->prev = NULL;
    } else {
        /* place it after orig */
        o_next = orig->next;
        orig->next = ir;
        ir->prev = orig;
    }
    /* now skip to the end of the ir list */
    while (ir->next) {
        ir = ir->next;
    }
    /* now fix up o_next to point back to ir */
    if (!o_next) {
        irl->tail = ir;
        ir->next = NULL;
    } else {
        ir->next = o_next;
        o_next->prev = ir;
    }
}

/*
 * Append a whole list
 */
void
AppendIRList(IRList *irl, IRList *sub)
{
  IR *last = irl->tail;
  if (!last) {
    irl->head = sub->head;
    irl->tail = sub->tail;
    return;
  }
  AppendIR(irl, sub->head);
}

/* Delete one IR from a list */
void
DeleteIR(IRList *irl, IR *ir)
{
  IR *prev = ir->prev;
  IR *next = ir->next;

  if (prev) {
    prev->next = next;
  } else {
    irl->head = next;
  }
  if (next) {
    next->prev = prev;
  } else {
    irl->tail = prev;
  }
}

// emit a machine instruction with no operands
static IR *EmitOp0(IRList *irl, Operandkind code)
{
  IR *ir = NewIR(code);
  AppendIR(irl, ir);
  return ir;
}

// emit a machine instruction with one operand
static IR *EmitOp1(IRList *irl, Operandkind code, Operand *op)
{
  IR *ir = NewIR(code);
  ir->dst = op;
  AppendIR(irl, ir);
  return ir;
}

// emit a machine instruction with two operands
static IR *EmitOp2(IRList *irl, Operandkind code, Operand *d, Operand *s)
{
  IR *ir = NewIR(code);
  ir->dst = d;
  ir->src = s;
  AppendIR(irl, ir);
  return ir;
}

// emit an assembler label
void EmitLabel(IRList *irl, Operand *op)
{
  EmitOp1(irl, OPC_LABEL, op);
}

// create a new temporary label name
char *
NewTempLabelName()
{
    static int lnum = 1;
    char *temp = malloc(32);
    sprintf(temp, "L_%03d_", lnum);
    lnum++;
    return temp;
}

Operand *
NewCodeLabel()
{
  Operand *label;
  label = NewOperand(IMM_COG_LABEL, NewTempLabelName(), 0);
  label->used = 0;
  return label;
}

Operand *
NewDataLabel()
{
  Operand *label;
  label = NewOperand(IMM_HUB_LABEL, NewTempLabelName(), 0);
  label->used = 0;
  return label;
}

void EmitLong(IRList *irl, int val)
{
  Operand *op;

  op = NewOperand(IMM_INT, "", val);
  EmitOp1(irl, OPC_LONG, op);
}

static void EmitLongPtr(IRList *irl, Operand *op)
{
  EmitOp1(irl, OPC_LONG, op);
}

static void EmitStringNoTrailingZero(IRList *irl, AST *ast)
{
  Operand *op;
  switch (ast->kind) {
  case AST_STRING:
      op = NewOperand(IMM_STRING, ast->d.string, 0);
      EmitOp1(irl, OPC_STRING, op);
      break;
  case AST_INTEGER:
      op = NewOperand(IMM_INT, "", ast->d.ival);
      EmitOp1(irl, OPC_BYTE, op);
      break;
  default:
      ERROR(ast, "Unable to emit string");
      break;
  }
}

void EmitString(IRList *irl, AST *ast)
{
  while (ast && ast->kind == AST_EXPRLIST) {
      EmitStringNoTrailingZero(irl, ast->left);
      ast = ast->right;
  }
  if (ast) {
      EmitStringNoTrailingZero(irl, ast);
  }
  // add a trailing 0
  EmitOp1(irl, OPC_BYTE, NewImmediate(0));
}

void EmitJump(IRList *irl, IRCond cond, Operand *label)
{
  IR *ir;

  if (cond == COND_FALSE) return;
  ir = NewIR(OPC_JUMP);
  ir->dst = label;
  ir->cond = cond;
  AppendIR(irl, ir);
}

//
// the header/footer are code that should only be emitted for
// non-inline function invocations, at the beginning and
// end of the function; they contain the labels and
// ret instruction, for example
//
static void EmitFunctionHeader(IRList *irl, Function *func)
{
    EmitLabel(irl, FuncData(func)->asmname);
}

static void EmitFunctionFooter(IRList *irl, Function *func)
{
    EmitLabel(irl, FuncData(func)->asmretname);
    EmitOp0(irl, OPC_RET);
}

//
// utility function: get a pointer to the n'th argument
// for function f
//
static Operand *GetFunctionParameter(IRList *irl, Function *func, int n)
{
#ifdef USE_GLOBAL_ARGS
    char temp[1024];
    sprintf(temp, "arg%d_", n+1);
    return GetGlobal(REG_ARG, strdup(temp), 0);
#else
    AST *astlist = func->params;
    AST *ast;

    while (astlist != NULL) {
        ast = astlist->left;
        astlist = astlist->right;
        if (n == 0) {
            return CompileIdentifierForFunc(irl, ast, func);
        }
        --n;
    }
    ERROR(NULL, "Too many parameters to function %s", func->name);
    return GetGlobal(REG_ARG, "dummyArg_", 0);
#endif
}

//
// the function Prolog and Epilog are always output, and do things
// like stack management and variable setup
//

static void EmitFunctionProlog(IRList *irl, Function *func)
{
    int n = 0;
    AST *astlist = func->params;
    AST *ast;
    Operand *src;
    Operand *dst;

    //
    // move parameters into local registers
    //
    while (astlist != NULL) {
        ast = astlist->left;
        astlist = astlist->right;

        src = GetFunctionParameter(irl, func, n++);
        dst = CompileIdentifierForFunc(irl, ast, func);
        EmitMove(irl, dst, src);
    }
}

static void EmitFunctionEpilog(IRList *irl, Function *func)
{
}

Operand *
NewImmediate(int32_t val)
{
  char temp[1024];
  if (val >= 0 && val < 512) {
    return NewOperand(IMM_INT, "", (int32_t)val);
  }
  sprintf(temp, "imm_%u_", (unsigned)val);
  return GetGlobal(IMM_INT, strdup(temp), (int32_t)val);
}

Operand *
NewImmediatePtr(Operand *val)
{
    char temp[1024];
    sprintf(temp, "ptr_%s_", val->name);
    return GetGlobal(IMM_HUB_LABEL, strdup(temp), (intptr_t)val);
}

static Operand *
GetFunctionTempRegister(Function *f, int n)
{
  Module *P = f->parse;
  char buf[1024];
  if (IsTopLevel(P)) {
      snprintf(buf, sizeof(buf)-1, "%s_tmp%03d_", f->name, n);
  } else {
      snprintf(buf, sizeof(buf)-1, "%s_%s_tmp%03d_", P->basename, f->name, n);
  }
  return GetGlobal(REG_LOCAL, strdup(buf), 0);
}

Operand *
NewFunctionTempRegister()
{
    Function *f = curfunc;
    IRFuncData *fdata = FuncData(f);

    fdata->curtempreg++;
    if (fdata->curtempreg > fdata->maxtempreg) {
        fdata->maxtempreg = fdata->curtempreg;
    }
    return GetFunctionTempRegister(f, fdata->curtempreg);
}

static Operand *
LabelRef(IRList *irl, Symbol *sym)
{
    Operand *temp;
    Label *lab = (Label *)sym->val;
    Module *P = current;
    
    if (!datbase) {
        datlabel = NewOperand(IMM_HUB_LABEL, IdentifierGlobalName(P, "dat_"), 0);
        datbase = NewImmediatePtr(datlabel);
    }
    temp = NewOperand(LONG_REF, (char *)datbase, lab->offset);
    return temp;
}

static Operand *
CompileIdentifierForFunc(IRList *irl, AST *expr, Function *func)
{
  Module *P = func->parse;
  Symbol *sym;
  const char *name = expr->d.string;
  AST *fcall;
  
  sym = LookupSymbolInFunc(func, name);
  if (sym) {
      switch (sym->type) {
      case SYM_PARAMETER:
 #ifdef USE_GLOBAL_ARGS
          return GetGlobal(REG_LOCAL, IdentifierLocalName(func, sym->name), 0);
 #else
          return GetGlobal(REG_ARG, IdentifierLocalName(func, sym->name), 0);
 #endif
      case SYM_VARIABLE:
          if (sym->flags & SYMF_GLOBAL) {
              Operand *addr = NewImmediate(sym->offset);
              return NewOperand(LONG_REF, (char *)addr, 0);
          }
          return GetGlobal(REG_REG, IdentifierGlobalName(P, sym->name), 0);
      case SYM_FUNCTION:
          fcall = NewAST(AST_FUNCCALL, expr, NULL);
          return CompileFunccall(irl, fcall);
      default:
          ERROR(expr, "Symbol %s is of a type not handle by PASM output yet", name);
          /* fall through */
      case SYM_LOCALVAR:
      case SYM_TEMPVAR:
      case SYM_RESULT:
          return GetGlobal(REG_LOCAL, IdentifierLocalName(func, name), 0);
      case SYM_LABEL:
          return LabelRef(irl, sym);
      }
  } else {
      ERROR(expr, "Unknown symbol %s", expr->d.string);
  }
  return GetGlobal(REG_LOCAL, IdentifierLocalName(func, name), 0);
}

Operand *
CompileIdentifier(IRList *irl, AST *expr)
{
    Symbol *sym = LookupSymbol(expr->d.string);
    if (sym && sym->type == SYM_CONSTANT) {
          AST *symexpr = (AST *)sym->val;
          int val = EvalConstExpr(symexpr);
          if (val >= 0 && val < 512) {
              return NewOperand(IMM_INT, sym->name, val);
          } else {
              return NewImmediate(val);
          }
    }
    return CompileIdentifierForFunc(irl, expr, curfunc);
}

Operand *
CompileHWReg(IRList *irl, AST *expr)
{
  HwReg *hw = (HwReg *)expr->d.ptr;
  return GetGlobal(REG_HW, hw->cname, 0);
}

static Operand *
CompileMul(IRList *irl, AST *expr, int gethi)
{
  Operand *lhs = CompileExpression(irl, expr->left);
  Operand *rhs = CompileExpression(irl, expr->right);
  Operand *temp = NewFunctionTempRegister();

  if (!mulfunc) {
    mulfunc = NewOperand(IMM_COG_LABEL, "multiply_", 0);
    mula = GetGlobal(REG_ARG, "muldiva_", 0);
    mulb = GetGlobal(REG_ARG, "muldivb_", 0);
  }
  EmitMove(irl, mula, lhs);
  EmitMove(irl, mulb, rhs);
  EmitOp1(irl, OPC_CALL, mulfunc);
  if (gethi) {
      EmitMove(irl, temp, mulb);
  } else {
      EmitMove(irl, temp, mula);
  }
  return temp;
}

static Operand *
CompileDiv(IRList *irl, AST *expr, int getmod)
{
  Operand *lhs = CompileExpression(irl, expr->left);
  Operand *rhs = CompileExpression(irl, expr->right);
  Operand *temp = NewFunctionTempRegister();

  if (!divfunc) {
    divfunc = NewOperand(IMM_COG_LABEL, "divide_", 0);
    diva = GetGlobal(REG_ARG, "muldiva_", 0);
    divb = GetGlobal(REG_ARG, "muldivb_", 0);
  }
  EmitMove(irl, diva, lhs);
  EmitMove(irl, divb, rhs);
  EmitOp1(irl, OPC_CALL, divfunc);
  if (getmod) {
      EmitMove(irl, temp, diva);
  } else {
      EmitMove(irl, temp, divb);
  }
  return temp;
}

static IRCond
CondFromExpr(int kind)
{
  switch (kind) {
  case T_NE:
    return COND_NE;
  case T_EQ:
    return COND_EQ;
  case T_GE:
    return COND_GE;
  case T_LE:
    return COND_LE;
  case '<':
    return COND_LT;
  case '>':
    return COND_GT;
  default:
    break;
  }
  ERROR(NULL, "Unknown boolean operator");
  return COND_FALSE;
}

/*
 * change condition for when the two sides of a comparison
 * are flipped (e.g. a<b becomes b>a)
 */
static IRCond
FlipSides(IRCond cond)
{
  switch (cond) {
  case COND_LT:
    return COND_GT;
  case COND_GT:
    return COND_LT;
  case COND_LE:
    return COND_GE;
  case COND_GE:
    return COND_LE;
  default:
    return cond;
  }
}
/*
 * invert a condition (e.g. EQ -> NE, LT -> GE
 * note that the conditions are declared in a
 * particular order to make this easier
 */
IRCond
InvertCond(IRCond v)
{
  v = (IRCond)(1 ^ (unsigned)v);
  return v;
}

static IRCond
CompileBasicBoolExpression(IRList *irl, AST *expr)
{
  IRCond cond;
  Operand *lhs;
  Operand *rhs;
  int opkind;
  IR *ir;
  int flags;

  if (expr->kind == AST_OPERATOR) {
    opkind = expr->d.ival;
  } else {
    opkind = -1;
  }
  switch(opkind) {
  case T_NE:
  case T_EQ:
  case '<':
  case '>':
  case T_LE:
  case T_GE:
    cond = CondFromExpr(opkind);
    lhs = CompileExpression(irl, expr->left);
    rhs = CompileExpression(irl, expr->right);
    break;
  default:
    cond = COND_NE;
    lhs = CompileExpression(irl, expr);
    rhs = NewOperand(IMM_INT, "", 0);
    break;
  }
  /* emit a compare operator */
  /* note that lhs cannot be a constant */
  if (lhs && lhs->kind == IMM_INT) {
    Operand *tmp = lhs;
    lhs = rhs;
    rhs = tmp;
    cond = FlipSides(cond);
  }
  switch (cond) {
  case COND_NE:
  case COND_EQ:
    flags = FLAG_WZ;
    break;
  default:
    flags = FLAG_WZ|FLAG_WC;
    break;
  }
  lhs = Dereference(irl, lhs);
  rhs = Dereference(irl, rhs);
  ir = EmitOp2(irl, OPC_CMPS, lhs, rhs);
  ir->flags |= flags;
  return cond;
}

void
CompileBoolBranches(IRList *irl, AST *expr, Operand *truedest, Operand *falsedest)
{
    IRCond cond;
    int opkind;
    Operand *dummylabel = NULL;
    IR *ir;
    
    if (expr->kind == AST_ISBETWEEN) {
        Operand *lo, *hi;
        Operand *val;
        Operand *tmp;
        tmp = NewFunctionTempRegister();
        val = CompileExpression(irl, expr->left);
        lo = CompileExpression(irl, expr->right->left);
        hi = CompileExpression(irl, expr->right->right);
        EmitMove(irl, tmp, lo);
        EmitOp2(irl, OPC_MAXS, lo, hi); // now lo really is the smallest
        EmitOp2(irl, OPC_MINS, hi, tmp); // and hi really is highest
        ir = EmitOp2(irl, OPC_CMPS, lo, val);
        ir->flags |= FLAG_WZ|FLAG_WC;
        ir = EmitOp2(irl, OPC_CMPS, val, hi);
        ir->flags |= FLAG_WZ|FLAG_WC;
        ir->cond = COND_LE;
        if (truedest) {
            EmitJump(irl, COND_LE, truedest);
        }
        if (falsedest) {
            EmitJump(irl, COND_GT, falsedest);
        }
        return;
    }
    
    if (expr->kind == AST_OPERATOR) {
        opkind = expr->d.ival;
    } else {
        opkind = -1;
    }

    switch (opkind) {
    case T_NOT:
        CompileBoolBranches(irl, expr->right, falsedest, truedest);
        break;
    case T_AND:
        if (!falsedest) {
            dummylabel = NewCodeLabel();
            falsedest = dummylabel;
        }
        CompileBoolBranches(irl, expr->left, NULL, falsedest);
        CompileBoolBranches(irl, expr->right, truedest, falsedest);
        if (dummylabel) {
            EmitLabel(irl, dummylabel);
        }
        break;
    case T_OR:
        if (!truedest) {
            dummylabel = NewCodeLabel();
            truedest = dummylabel;
        }
        CompileBoolBranches(irl, expr->left, truedest, NULL);
        CompileBoolBranches(irl, expr->right, truedest, falsedest);
        if (dummylabel) {
            EmitLabel(irl, dummylabel);
        }
        break;
    default:
        if (IsConstExpr(expr)) {
            cond = EvalConstExpr(expr) == 0 ? COND_FALSE : COND_TRUE;
        } else {
            cond = CompileBasicBoolExpression(irl, expr);
        }
        if (truedest) {
            EmitJump(irl, cond, truedest);
        }
        if (falsedest) {
            EmitJump(irl, InvertCond(cond), falsedest);
        }
        break;
    }
}

static int
OpcFromOp(int op)
{
  switch(op) {
  case '+':
    return OPC_ADD;
  case '-':
    return OPC_SUB;
  case '&':
    return OPC_AND;
  case '|':
    return OPC_OR;
  case '^':
    return OPC_XOR;
  case T_SHL:
    return OPC_SHL;
  case T_SAR:
    return OPC_SAR;
  case T_SHR:
    return OPC_SHR;
  case T_NEGATE:
    return OPC_NEG;
  case T_ABS:
    return OPC_ABS;
  case T_ROTL:
      return OPC_ROL;
  case T_ROTR:
      return OPC_ROR;
  case T_REV:
      return OPC_REV;
  case T_LIMITMIN:
      return OPC_MINS;
  case T_LIMITMAX:
      return OPC_MAXS;
  default:
    ERROR(NULL, "Unsupported operator %d", op);
    return OPC_UNKNOWN;
  }
}
static Operand *
Dereference(IRList *irl, Operand *op)
{
    if (IsMemRef(op)) {
        Operand *temp = NewFunctionTempRegister();
        EmitMove(irl, temp, op);
        return temp;
    }
    return op;
}

static Operand *
CompileBasicOperator(IRList *irl, AST *expr)
{
  int op = expr->d.ival;
  AST *lhs = expr->left;
  AST *rhs = expr->right;
  Operand *left;
  Operand *right;
  Operand *temp = NewFunctionTempRegister();
  IR *ir;

  switch(op) {
  case '+':
  case '-':
  case '^':
  case '&':
  case '|':
  case T_SHL:
  case T_SHR:
  case T_SAR:
  case T_ROTL:
  case T_ROTR:
  case T_LIMITMIN:
  case T_LIMITMAX:
    left = CompileExpression(irl, lhs);
    right = CompileExpression(irl, rhs);
    EmitMove(irl, temp, left);
    right = Dereference(irl, right);
    EmitOp2(irl, OpcFromOp(op), temp, right);
    return temp;
  case T_NEGATE:
  case T_ABS:
  case T_REV:
    right = CompileExpression(irl, rhs);
    right = Dereference(irl, right);
    EmitOp2(irl, OpcFromOp(op), temp, right);
    return temp;
  case T_BIT_NOT:
    right = CompileExpression(irl, rhs);
    EmitMove(irl, temp, right);
    left = NewImmediate(0xFFFFFFFF);
    EmitOp2(irl, OPC_XOR, temp, left);
    return temp;
  case T_ENCODE:
    right = CompileExpression(irl, rhs);
    left = NewFunctionTempRegister();
    EmitMove(irl, left, right);
    EmitMove(irl, temp, NewImmediate(32));
    right = NewCodeLabel();
    EmitLabel(irl, right);
    ir = EmitOp2(irl, OPC_SHL, left, NewImmediate(1));
    ir->flags |= FLAG_WC;
    ir = EmitOp2(irl, OPC_DJNZ, temp, right);
    ir->cond = COND_NC;
    return temp;
  case T_DECODE:
    ERROR(rhs, "Internal error: decode operators should have been handled in spin transormations");
    return NewImmediate(0);

  case T_NOT:
  case T_AND:
  case T_OR:
  case T_EQ:
  case T_NE:
  case T_LE:
  case T_GE:
  case '<':
  case '>':
  {
      Operand *zero = NewImmediate(0);
      Operand *skiplabel = NewCodeLabel();
      EmitMove(irl, temp, zero);
      CompileBoolBranches(irl, expr, NULL, skiplabel);
      EmitOp2(irl, OPC_XOR, temp, NewImmediate(0xFFFFFFFF));
      EmitLabel(irl, skiplabel);
      return temp;
  }
  
  default:
    ERROR(lhs, "Unsupported operator %d", op);
    return NewImmediate(0);
  }
}

static Operand *
CompileOperator(IRList *irl, AST *expr)
{
    int op = expr->d.ival;
    switch (op) {
    case T_INCREMENT:
    case T_DECREMENT:
    {
        AST *addone;
        Operand *lhs;
        Operand *temp = NULL;
        int opc = (op == T_INCREMENT) ? '+' : '-';
        if (expr->left) {  /* x++ */
            addone = AstAssign(opc, expr->left, AstInteger(1));
            temp = NewFunctionTempRegister();
            lhs = CompileExpression(irl, expr->left);
            EmitMove(irl, temp, lhs);
            CompileExpression(irl, addone);
            return  temp;
        } else {
            addone = AstAssign(opc, expr->right, AstInteger(-1));
        }
        return CompileExpression(irl, addone);
    }
    case '*':
        return CompileMul(irl, expr, 0);
    case T_HIGHMULT:
        return CompileMul(irl, expr, 1);
    case '/':
        return CompileDiv(irl, expr, 0);
    case T_MODULUS:
        return CompileDiv(irl, expr, 1);
    case '&':
        if (expr->right->kind == AST_OPERATOR && expr->right->d.ival == T_BIT_NOT) {
	  Operand *lhs = CompileExpression(irl, expr->left);
	  Operand *rhs = CompileExpression(irl, expr->right->right);
	  Operand *temp = NewFunctionTempRegister();
	  EmitMove(irl, temp, lhs);
	  EmitOp2(irl, OPC_ANDN, temp, rhs);
	  return temp;
        }
        return CompileBasicOperator(irl, expr);
    default:
        return CompileBasicOperator(irl, expr);
    }
}

static void
AppendOperand(OperandList **listptr, Operand *op)
{
  OperandList *next = malloc(sizeof(OperandList));
  OperandList *x;
  next->op = op;
  next->next = NULL;
  for(;;) {
    x = *listptr;
    if (!x) {
      *listptr = next;
      return;
    }
    listptr = &x->next;
  }
}

static OperandList quitstack;

static void
PushQuitNext(Operand *q, Operand *n)
{
  OperandList *qholder = malloc(sizeof(OperandList));
  OperandList *nholder = malloc(sizeof(OperandList));
  qholder->op = quitlabel;
  nholder->op = nextlabel;
  qholder->next = nholder;
  nholder->next = quitstack.next;
  quitstack.next = qholder;

  quitlabel = q;
  nextlabel = n;
}

static void
PopQuitNext()
{
  OperandList *ql, *nl;

  ql = quitstack.next;
  if (!ql || !ql->next) {
    ERROR(NULL, "Internal error: empty loop stack");
    return;
  }
  nl = ql->next;
  quitstack.next = nl->next;
  quitlabel = ql->op;
  nextlabel = nl->op;
  free(nl);
  free(ql);
}

/* compiles a list of expressions; returns the last one
 * tolist is a list of where to store the expressions
 * (may be NULL if we don't care about saving them)
 */
static OperandList *
CompileExprList(IRList *irl, AST *fromlist)
{
  AST *from;
  Operand *opfrom = NULL;
  Operand *opto = NULL;
  OperandList *ret = NULL;

  while (fromlist) {
    from = fromlist->left;
    fromlist = fromlist->right;

    opfrom = CompileExpression(irl, from);
    opto = NewFunctionTempRegister();
    if (!opfrom) break;
    EmitMove(irl, opto, opfrom);
    AppendOperand(&ret, opto);
  }
  return ret;
}

static void
EmitParameterList(IRList *irl, OperandList *oplist, Function *func)
{
    Operand *op;
    Operand *dst;
    int n = 0;
  
    while (oplist != NULL) {
        op = oplist->op;
        oplist = oplist->next;

        dst = GetFunctionParameter(irl, func, n++);
        EmitMove(irl, dst, op);
    }
}

static Operand *
CompileFunccall(IRList *irl, AST *expr)
{
  Operand *result;
  Symbol *sym;
  Function *func;
  AST *params;
  OperandList *temp;
  IR *ir;

  /* compile the function operands */
  sym = FindFuncSymbol(expr, NULL, NULL);
  if (!sym || sym->type != SYM_FUNCTION) {
    ERROR(expr, "expected function symbol");
    return NewImmediate(0);
  }
  func = (Function *)sym->val;
  params = expr->right;
  temp = CompileExprList(irl, params);

  /* now copy the parameters into place (have to do this in case there are
     function calls within the parameters)
   */
  EmitParameterList(irl, temp, func);

  /* emit the call */
  ir = EmitOp1(irl, OPC_CALL, FuncData(func)->asmname);
  ir->aux = (void *)func; // remember the function for optimization purposes

  /* now get the result */
  result = GetGlobal(REG_REG, "result_", 0);
  return result;
}

static Operand *
CompileMemref(IRList *irl, AST *expr)
{
  Operand *addr = CompileExpression(irl, expr->right);
  Operand *temp;
  AST *type = expr->left;
  int size = EvalConstExpr(type->left);

  if (size == 1) {
      temp = NewOperand(BYTE_REF, (char *)addr, 0);
  } else if (size == 2) {
      temp = NewOperand(WORD_REF, (char *)addr, 0);
  } else {
      temp = NewOperand(LONG_REF, (char *)addr, 0);
      if (size != 4) {
          ERROR(expr, "Illegal size for memory reference");
      }
  }
  return temp;
}

static Operand *
ApplyArrayIndex(IRList *irl, Operand *base, Operand *offset)
{
    Operand *basereg;
    Operand *newbase;
    int idx;
    int siz;
    int shift;
    
    if (!IsMemRef(base)) {
        ERROR(NULL, "Array does not reference memory");
        return base;
    }
    switch (base->kind) {
    case LONG_REF:
        siz = 4;
        shift = 2;
        break;
    case WORD_REF:
        siz = 2;
        shift = 1;
        break;
    case BYTE_REF:
        siz = 1;
        shift = 0;
        break;
    default:
        ERROR(NULL, "Bad size in array reference");
        siz = 1;
        break;
    }
          
    basereg = (Operand *)base->name;
    if (offset->kind == IMM_INT) {
        idx = offset->val * siz;
        if (idx == 0) {
            return base;
        }
        newbase = NewOperand(base->kind, (char *)basereg, idx + base->val);
        return newbase;
    }
    newbase = NewFunctionTempRegister();
    EmitMove(irl, newbase, offset);
    if (shift) {
        EmitOp2(irl, OPC_SHL, newbase, NewImmediate(shift));
    }
    EmitOp2(irl, OPC_ADD, newbase, basereg);
    return NewOperand(base->kind, (char *)newbase, 0);
}

static Operand *
CompileCondResult(IRList *irl, AST *expr)
{
    AST *cond = expr->left;
    AST *ifpart = expr->right->left;
    AST *elsepart = expr->right->right;
    Operand *r = NewFunctionTempRegister();
    Operand *tmp;
    Operand *label1 = NewCodeLabel();
    Operand *label2 = NewCodeLabel();

    CompileBoolBranches(irl, cond, NULL, label1);
    /* the default is the IF part */
    tmp = CompileExpression(irl, ifpart);
    EmitMove(irl, r, tmp);
    EmitJump(irl, COND_TRUE, label2);

    /* here is the ELSE part */
    EmitLabel(irl, label1);
    tmp = CompileExpression(irl, elsepart);
    EmitMove(irl, r, tmp);

    /* all done */
    EmitLabel(irl, label2);

    return r;
}

//
// emit a load effective address
//
static void
EmitLea(IRList *irl, Operand *dst, Operand *src)
{
    if (IsMemRef(src)) {
        int off = src->val;
        src = (Operand *)src->name;
        EmitMove(irl, dst, src);
        EmitAddSub(irl, dst, off);
    } else {
        ERROR(NULL, "Load Effective Address on a non-memory reference");
    }
}

//
// get the address of an expression
//
static Operand *
GetAddressOf(IRList *irl, AST *expr)
{
    Operand *res;
    Operand *tmp;
    switch (expr->kind) {
    case AST_IDENTIFIER:
        tmp = NewFunctionTempRegister();
        res = CompileExpression(irl, expr);
        EmitLea(irl, tmp, res);
        return tmp;
    default:
        ERROR(expr, "Cannot take address of expression\n");
        break;
    }
    return NewImmediate(-1);
}

static Operand *
CompileExpression(IRList *irl, AST *expr)
{
  Operand *r;
  Operand *val;

  while (expr && expr->kind == AST_COMMENTEDNODE) {
    expr = expr->left;
  }
  if (!expr) return NULL;
  if (IsConstExpr(expr)) {
      switch (expr->kind) {
      case AST_IDENTIFIER:
          // leave symbolic constants alone
      case AST_ADDROF:
      case AST_ABSADDROF:
          // similarly for address expressions
          break;
      default:
          expr = FoldIfConst(expr);
          break;
      }
  }
  switch (expr->kind) {
  case AST_CONDRESULT:
      return CompileCondResult(irl, expr);
  case AST_ISBETWEEN:
  {
      Operand *zero = NewImmediate(0);
      Operand *skiplabel = NewCodeLabel();
      Operand *temp = NewFunctionTempRegister();
      
      EmitMove(irl, temp, zero);
      CompileBoolBranches(irl, expr, NULL, skiplabel);
      EmitOp2(irl, OPC_XOR, temp, NewImmediate(0xFFFFFFFF));
      EmitLabel(irl, skiplabel);
      return temp;
  }
  case AST_SEQUENCE:
      r = CompileExpression(irl, expr->left);
      r = CompileExpression(irl, expr->right);
      return r;
  case AST_INTEGER:
  case AST_FLOAT:
    r = NewImmediate((int32_t)expr->d.ival);
    return r;
  case AST_RESULT:
    return CompileExpression(irl, curfunc->resultexpr);
  case AST_IDENTIFIER:
    return CompileIdentifier(irl, expr);
  case AST_HWREG:
    return CompileHWReg(irl, expr);
  case AST_OPERATOR:
    return CompileOperator(irl, expr);
  case AST_FUNCCALL:
    return CompileFunccall(irl, expr);
  case AST_ASSIGN:
    r = CompileExpression(irl, expr->left);
    val = CompileExpression(irl, expr->right);
    EmitMove(irl, r, val);
    return r;
  case AST_RANGEREF:
    return CompileExpression(irl, TransformRangeUse(expr));
  case AST_STRING:
      if (strlen(expr->d.string) > 1)  {
            ERROR(expr, "string too long, expected a single character");
      }
      return NewImmediate(expr->d.string[0]);
  case AST_STRINGPTR:
      r = GetHub(STRING_DEF, NewTempLabelName(), (intptr_t)(expr->left));
      return NewImmediatePtr(r);
  case AST_ARRAYREF:
  {
      Operand *base;
      Operand *offset;

      if (!expr->right) {
          ERROR(expr, "Array ref with no index?");
          return NewOperand(REG_REG, "???", 0);
      }
      base = CompileExpression(irl, expr->left);
      offset = CompileExpression(irl, expr->right);
      return ApplyArrayIndex(irl, base, offset);
  }
  case AST_MEMREF:
    return CompileMemref(irl, expr);
  case AST_COGINIT:
    ERROR(expr, "Cannot handle cognew/coginit yet");
    return NewOperand(REG_REG, "???", 0);
  case AST_ADDROF:
  case AST_ABSADDROF:
      return GetAddressOf(irl, expr->left);
  default:
    ERROR(expr, "Cannot handle expression yet");
    return NewOperand(REG_REG, "???", 0);
  }
}

static void EmitStatement(IRList *irl, AST *ast); /* forward declaration */

static void
EmitStatementList(IRList *irl, AST *ast)
{
    while (ast) {
        if (ast->kind != AST_STMTLIST) {
            ERROR(ast, "Internal error: expected statement list, got %d",
                  ast->kind);
            return;
        }
        EmitStatement(irl, ast->left);
        ast = ast->right;
    }
}
static void EmitAddSub(IRList *irl, Operand *dst, int off)
{
    int opc  = OPC_ADD;
    Operand *imm;
    
    if (off < 0) {
        off = -off;
        opc = OPC_SUB;
    }
    imm = NewImmediate(off);
    EmitOp2(irl, opc, dst, imm);
}

static void EmitMove(IRList *irl, Operand *origdst, Operand *origsrc)
{
    Operand *temp;
    Operand *dst = origdst;
    Operand *src = origsrc;
    
    if (IsMemRef(src)) {
        int off = src->val;
        temp = NewFunctionTempRegister();
        src = (Operand *)src->name;
        switch (origsrc->kind) {
        default:
            ERROR(NULL, "Illegal memory reference");
            break;
        case LONG_REF:
            if (off) {
                EmitAddSub(irl, src, off);
            }
            EmitOp2(irl, OPC_RDLONG, temp, src);
            if (off) {
                EmitAddSub(irl, src, -off);
            }
            break;
        case WORD_REF:
            if (off) {
                EmitAddSub(irl, src, off);
            }
            EmitOp2(irl, OPC_RDWORD, temp, src);
            if (off) {
                EmitAddSub(irl, src, -off);
            }
            break;
        case BYTE_REF:
            if (off) {
                EmitAddSub(irl, src, off);
            }
            EmitOp2(irl, OPC_RDBYTE, temp, src);
            if (off) {
                EmitAddSub(irl, src, -off);
            }
            break;
        }
        src = temp;
    }
    if (IsMemRef(origdst)) {
        int off = dst->val;
        dst = (Operand *)dst->name;
        if (src->kind == IMM_INT) {
            Operand *temp = NewFunctionTempRegister();
            EmitMove(irl, temp, src);
            src = temp;
        }
        switch (origdst->kind) {
        default:
            ERROR(NULL, "Illegal memory reference");
            break;
        case LONG_REF:
            if (off) {
                EmitAddSub(irl, dst, off);
            }
            EmitOp2(irl, OPC_WRLONG, src, dst);
            if (off) {
                EmitAddSub(irl, dst, -off);
            }            
            break;
        case WORD_REF:
            if (off) {
                EmitAddSub(irl, dst, off);
            }
            EmitOp2(irl, OPC_WRWORD, src, dst);
            if (off) {
                EmitAddSub(irl, dst, -off);
            }
            break;
        case BYTE_REF:
            if (off) {
                EmitAddSub(irl, dst, off);
            }
            EmitOp2(irl, OPC_WRBYTE, src, dst);
            if (off) {
                EmitAddSub(irl, dst, -off);
            }
            break;
        }
    } else {
        EmitOp2(irl, OPC_MOVE, dst, src);
    }
}

static void
FreeTempRegisters(IRList *irl, int starttempreg)
{
    int endtempreg;
    Operand *op;

    /* release temporaries we used */
    endtempreg = FuncData(curfunc)->curtempreg;
    FuncData(curfunc)->curtempreg = starttempreg;

    /* and mark them as dead */
    while (endtempreg > starttempreg) {
      op = GetFunctionTempRegister(curfunc, endtempreg);
      EmitOp1(irl, OPC_DEAD, op);
      --endtempreg;
    }
}

//
// a for loop gets a pattern like
//
//   initial code
// Ltop:
//   if (!loopcond) goto Lexit
//   loop body
// Lnext:
//   updatestmt
//   goto Ltop
// Lexit
//
// if it must execute at least once, it looks like:
//   initial code
// Ltop:
//   loop body
// Lnext
//   updatestmt
//   if (loopcond) goto Ltop
// Lexit
//

static void EmitForLoop(IRList *irl, AST *ast, int atleastonce)
{
    AST *initstmt;
    AST *loopcond;
    AST *update;
    AST *body = 0;

    Operand *toplabel, *nextlabel, *exitlabel;
    initstmt = ast->left;
    ast = ast->right;
    if (!ast || ast->kind != AST_TO) {
        ERROR(ast, "Internal Error: expected AST_TO in for loop");
        return;
    }
    loopcond = ast->left;
    ast = ast->right;
    if (!ast || ast->kind != AST_STEP) {
        ERROR(ast, "Internal Error: expected AST_TO in for loop");
        return;
    }
    update = ast->left;
    ast = ast->right;
    body = ast;

    CompileExpression(irl, initstmt);
    
    toplabel = NewCodeLabel();
    nextlabel = NewCodeLabel();
    exitlabel = NewCodeLabel();
    PushQuitNext(exitlabel, nextlabel);

    EmitLabel(irl, toplabel);
    if (!atleastonce) {
        CompileBoolBranches(irl, loopcond, NULL, exitlabel);
    }
    EmitStatementList(irl, body);
    EmitLabel(irl, nextlabel);
    EmitStatement(irl, update);
    if (atleastonce) {
        CompileBoolBranches(irl, loopcond, toplabel, NULL);
    } else {
        EmitJump(irl, COND_TRUE, toplabel);
    }
    EmitLabel(irl, exitlabel);
    PopQuitNext();
}

static void CompileWait(IRList *irl, AST *origast)
{
    AST *ast = origast;
    AST *arg0, *arg1, *arg2;
    int opc;
    Operand *lhs, *rhs;
    Operand *tmp;

    ast = origast->left; // pick up argument list
    arg0 = ast->left; ast = ast->right;
    arg1 = ast->left; ast = ast->right;
    arg2 = ast->left;
    if (EvalConstExpr(arg2) != 0) {
        ERROR(ast, "wait command only works on port 0");
        return;
    }
    if (origast->kind == AST_WAITPEQ) {
        opc = OPC_WAITPEQ;
    } else if (origast->kind == AST_WAITPNE) {
        opc = OPC_WAITPNE;
    } else {
        ERROR(ast, "Internal error, unexpected wait");
        return;
    }
    lhs = CompileExpression(irl, arg0);
    if (!IsRegister(lhs->kind)) {
        tmp = NewFunctionTempRegister();
        EmitMove(irl, tmp, lhs);
        lhs = tmp;
    }
    rhs = CompileExpression(irl, arg1);
    rhs = Dereference(irl, rhs);
    EmitOp2(irl, opc, lhs, rhs);
}

static void EmitStatement(IRList *irl, AST *ast)
{
    AST *retval;
    Operand *op;
    Operand *result;
    Operand *botloop, *toploop;
    Operand *exitloop;
    int starttempreg;

    if (!ast) return;

    starttempreg = FuncData(curfunc)->curtempreg;
    switch (ast->kind) {
    case AST_COMMENTEDNODE:
        EmitStatement(irl, ast->left);
        break;
    case AST_RETURN:
        retval = ast->left;
        if (!retval) {
            retval = curfunc->resultexpr;
        }
	if (retval) {
	    op = CompileExpression(irl, retval);
	    result = GetGlobal(REG_REG, "result_", 0);
            EmitMove(irl, result, op);
	}
	EmitJump(irl, COND_TRUE, FuncData(curfunc)->asmretname);
	break;
    case AST_WAITCNT:
        retval = ast->left;
        if (!retval) {
            ERROR(ast, "No expression for waitcnt");
            return;
        }
        op = CompileExpression(irl, retval);
        EmitOp2(irl, OPC_WAITCNT, op, NewImmediate(0));
        break;
    case AST_WAITPEQ:
    case AST_WAITPNE:
        CompileWait(irl, ast);
        break;
    case AST_WHILE:
        toploop = NewCodeLabel();
	botloop = NewCodeLabel();
	PushQuitNext(botloop, toploop);
	EmitLabel(irl, toploop);
        CompileBoolBranches(irl, ast->left, NULL, botloop);
	FreeTempRegisters(irl, starttempreg);
        EmitStatementList(irl, ast->right);
	EmitJump(irl, COND_TRUE, toploop);
	EmitLabel(irl, botloop);
	PopQuitNext();
	break;
    case AST_DOWHILE:
        toploop = NewCodeLabel();
	botloop = NewCodeLabel();
	exitloop = NewCodeLabel();
	PushQuitNext(exitloop, botloop);
	EmitLabel(irl, toploop);
        EmitStatementList(irl, ast->right);
	EmitLabel(irl, botloop);
        CompileBoolBranches(irl, ast->left, toploop, NULL);
	FreeTempRegisters(irl, starttempreg);
	EmitLabel(irl, exitloop);
	PopQuitNext();
	break;
    case AST_FORATLEASTONCE:
    case AST_FOR:
	EmitForLoop(irl, ast, ast->kind == AST_FORATLEASTONCE);
        break;
    case AST_QUIT:
        if (!quitlabel) {
	    ERROR(ast, "loop exit statement outside of loop");
	} else {
	    EmitJump(irl, COND_TRUE, quitlabel);
	}
	break;
    case AST_NEXT:
        if (!nextlabel) {
	    ERROR(ast, "loop continue statement outside of loop");
	} else {
	    EmitJump(irl, COND_TRUE, nextlabel);
	}
	break;
    case AST_IF:
        toploop = NewCodeLabel();
        CompileBoolBranches(irl, ast->left, NULL, toploop);
	FreeTempRegisters(irl, starttempreg);
	ast = ast->right;
	if (ast->kind == AST_COMMENTEDNODE) {
	  ast = ast->left;
	}
	/* ast should be an AST_THENELSE */
	EmitStatementList(irl, ast->left);
	if (ast->right) {
	  botloop = NewCodeLabel();
	  EmitJump(irl, COND_TRUE, botloop);
	  EmitLabel(irl, toploop);
	  EmitStatementList(irl, ast->right);
	  EmitLabel(irl, botloop);
	} else {
	  EmitLabel(irl, toploop);
	}
	break;
    case AST_YIELD:
	/* do nothing in assembly for YIELD */
        break;
    case AST_ASSIGN:
        if (ast->left && ast->left->kind == AST_RANGEREF) {
            ast = TransformRangeAssign(ast->left, ast->right);
        }
        /* fall through */
    default:
        /* assume an expression */
        (void)CompileExpression(irl, ast);
        break;
    }
    FreeTempRegisters(irl, starttempreg);
}

/*
 * compile just the body of a function, and put it
 * in an IRL for that function
 */
static void
CompileFunctionBody(Function *f)
{
    IRList *irl = FuncIRL(f);
    if (f->is_recursive) {
        ERROR(f->body, "Recursive function %s not supported in PASM", f->name);
    }
    nextlabel = quitlabel = NULL;
    EmitFunctionProlog(irl, f);
    EmitStatementList(irl, f->body);
    EmitFunctionEpilog(irl, f);
    OptimizeIRLocal(irl);
}

/*
 * compile a function to IR and put it at the end of the IRList
 */

static void
CompileWholeFunction(IRList *irl, Function *f)
{
    IRList *firl = FuncIRL(f);

    EmitFunctionHeader(irl, f);
    AppendIRList(irl, firl);
    EmitFunctionFooter(irl, f);
}

static Operand *newlineOp;
static void EmitNewline(IRList *irl)
{
  IR *ir = NewIR(OPC_COMMENT);
  ir->dst = newlineOp;
  AppendIR(irl, ir);
}

static int gcmpfunc(const void *a, const void *b)
{
  const AsmVariable *ga = (const AsmVariable *)a;
  const AsmVariable *gb = (const AsmVariable *)b;
  return strcmp(ga->op->name, gb->op->name);
}

static void EmitAsmVars(struct flexbuf *fb, IRList *irl, int alphaSort)
{
    size_t siz = flexbuf_curlen(fb) / sizeof(AsmVariable);
    size_t i;
    AsmVariable *g = (AsmVariable *)flexbuf_peek(fb);

    if (siz > 0) {
      EmitNewline(irl);
    }
    /* sort the global variables */
    if (alphaSort) {
        qsort(g, siz, sizeof(*g), gcmpfunc);
    }
    for (i = 0; i < siz; i++) {
      if (g[i].op->kind == REG_LOCAL && !g[i].op->used) {
	continue;
      }
      if (g[i].op->kind == IMM_INT && !g[i].op->used) {
	continue;
      }
      if (g[i].op->kind == REG_HW) {
	continue;
      }
      EmitLabel(irl, g[i].op);
      if (g[i].op->kind == STRING_DEF) {
          EmitString(irl, (AST *)g[i].val);
      } else if (g[i].op->kind == IMM_COG_LABEL || g[i].op->kind == IMM_HUB_LABEL) {
          EmitLongPtr(irl, (Operand *)g[i].op->val);
      } else {
          EmitLong(irl, g[i].val);
      }
    }
}
void EmitGlobals(IRList *irl)
{
    EmitAsmVars(&cogGlobalVars, irl, 1);
    EmitAsmVars(&hubGlobalVars, irl, 0);
}

bool
CompileToIR(IRList *irl, Module *P)
{
    Function *f;

    // assign all function names so we can do forward calls
    // this is also where we can allocate the back end data
    for(f = P->functions; f; f = f->next) {
	const char *fname;
        char *frname;
        fname = IdentifierGlobalName(P, f->name);
	frname = malloc(strlen(fname) + 8);
	sprintf(frname, "%s_ret", fname);
        f->bedata = calloc(1, sizeof(IRFuncData));
        FuncData(f)->asmname = NewOperand(IMM_COG_LABEL, fname, 0);
        FuncData(f)->asmretname = NewOperand(IMM_COG_LABEL, frname, 0);
    }
    
    // now compile the functions
    for (f = P->functions; f; f = f->next) {
      curfunc = f;
      CompileFunctionBody(f);
      FuncData(f)->isInline = ShouldBeInlined(f);
    }

    // 2nd pass, to expand inlines
    for (f = P->functions; f; f = f->next) {
        IRList *firl = FuncIRL(f);
        curfunc = f;
        if (ExpandInlines(firl)) {
            // may be new opportunities for optimization
            OptimizeIRLocal(firl);
        }
    }
    
    // finally emit output
    for(f = P->functions; f; f = f->next) {
        curfunc = f;
	if (newlineOp) {
  	    EmitNewline(irl);
	} else {
	  newlineOp = NewOperand(IMM_STRING, "\n", 0);
	}
        CompileWholeFunction(irl, f);
    }
    return gl_errors == 0;
}

/*
 * emit builtin functions like mul and div
 */
static const char *builtin_mul =
"\nmultiply_\n"
"\tmov\titmp2_, muldiva_\n"
"\txor\titmp2_, muldivb_\n"
"\tabs\tmuldiva_, muldiva_\n"
"\tabs\tmuldivb_, muldivb_\n"
"\tmov\tresult_, #0\n"
"\tmov\titmp1_, #32\n"
"\tshr\tmuldiva_, #1 wc\n"
"mul_lp_\n"
" if_c\tadd\tresult_, muldivb_ wc\n"
"\trcr\tresult_, #1 wc\n"
"\trcr\tmuldiva_, #1 wc\n"
"\tdjnz\titmp1_, #mul_lp_\n"

"\tshr\titmp2_, #31 wz\n"
" if_nz\tneg\tresult_, result_\n"
" if_nz\tneg\tmuldiva_, muldiva_ wz\n"
" if_nz\tsub\tresult_, #1\n"
"\tmov\tmuldivb_, result_\n"
"multiply__ret\n"
"\tret\n"
;

/*
 * signed divide, taken from spin interpreter
 */

static const char *builtin_div =
"' code originally from spin interpreter, modified slightly\n"
"\ndivide_\n"
"       abs     muldiva_,muldiva_     wc       'abs(x)\n"
"       muxc    itmp2_,#%11                    'store sign of x\n"
"       abs     muldivb_,muldivb_     wc,wz    'abs(y)\n"
" if_c  xor     itmp2_,#%10                    'store sign of y\n"

"        mov     itmp1_,#0                    'unsigned divide\n"
"        mov     CNT,#32             ' use CNT shadow register\n"
"mdiv__\n"
"        shr     muldivb_,#1        wc,wz\n"
"        rcr     itmp1_,#1\n"
" if_nz   djnz    CNT,#mdiv__\n"
"mdiv2__\n"
"        cmpsub  muldiva_,itmp1_        wc\n"
"        rcl     muldivb_,#1\n"
"        shr     itmp1_,#1\n"
"        djnz    CNT,#mdiv2__\n"

"        test    itmp2_,#1        wc       'restore sign, remainder\n"
"        negc    muldiva_,muldiva_ \n"
"        test    itmp2_,#%10      wc       'restore sign, division result\n"
"        negc    muldivb_,muldivb_\n"

"divide__ret\n"
    "\tret\n"
;

static void
EmitBuiltins(IRList *irl)
{
    if (mulfunc) {
        Operand *loop = NewOperand(IMM_STRING, builtin_mul, 0);
        EmitOp1(irl, OPC_COMMENT, loop);
        (void)GetGlobal(REG_REG, "itmp1_", 0);
        (void)GetGlobal(REG_REG, "itmp2_", 0);
        (void)GetGlobal(REG_REG, "result_", 0);
    }
    if (divfunc) {
        Operand *loop = NewOperand(IMM_STRING, builtin_div, 0);
        EmitOp1(irl, OPC_COMMENT, loop);
        (void)GetGlobal(REG_REG, "itmp1_", 0);
        (void)GetGlobal(REG_REG, "itmp2_", 0);
        (void)GetGlobal(REG_REG, "result_", 0);
    }
}

static void
CompileConstant(IRList *irl, AST *ast)
{
    const char *name = ast->d.string;
    Symbol *sym;
    AST *expr;
    int32_t val;
    Operand *op1, *op2;
    
    if (!IsConstExpr(ast)) {
        ERROR(ast, "%s is not constant", name);
        return;
    }
    sym = LookupSymbol(name);
    if (!sym) {
        ERROR(ast, "constant symbol %s not declared??", name);
        return;
    }
    expr = (AST *)sym->val;
    val = EvalConstExpr(expr);
    op1 = NewOperand(IMM_STRING, name, val);
    op2 = NewImmediate(val);
    EmitOp2(irl, OPC_CONST, op1, op2);
}

static void
CompileConsts(IRList *irl, AST *conblock)
{
    AST *ast, *upper;

    for (upper = conblock; upper; upper = upper->right) {
        ast = upper->left;
        if (ast->kind == AST_COMMENTEDNODE) {
            ast = ast->left;
        }
        switch (ast->kind) {
        case AST_ENUMSKIP:
            CompileConstant(irl, ast->left);
            break;
        case AST_IDENTIFIER:
            CompileConstant(irl, ast);
            break;
        case AST_ASSIGN:
            CompileConstant(irl, ast->left);
            break;
        default:
            /* do nothing */
            break;
        }
    }
}

void
EmitDatSection(IRList *irl, Module *P)
{
  Flexbuf fb;
  Operand *op;
  char *data;
  int len;

  if (!datbase)
      return;
  flexbuf_init(&fb, 32768);
  PrintDataBlock(&fb, P, BINARY_OUTPUT);
  len = flexbuf_curlen(&fb);
  data = flexbuf_get(&fb);
  op = NewOperand(IMM_STRING, data, len);
  EmitOp2(irl, OPC_LABELED_BLOB, datlabel, op);
}

void
OutputAsmCode(const char *fname, Module *P)
{
    FILE *f = NULL;
    Module *save;
    IRList irl;
    const char *asmcode;
    
    save = current;
    current = P;

    irl.head = NULL;
    irl.tail = NULL;

    CompileConsts(&irl, P->conblock);
    if (!CompileToIR(&irl, P)) {
        return;
    }
    OptimizeIRGlobal(&irl);
    EmitBuiltins(&irl);
    EmitGlobals(&irl);
    EmitDatSection(&irl, P);
    asmcode = IRAssemble(&irl);
    
    current = save;

    f = fopen(fname, "wb");
    if (!f) {
        perror(fname);
        exit(1);
    }
    fwrite(asmcode, 1, strlen(asmcode), f);
    fclose(f);
}

