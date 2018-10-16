//
//  toy.cpp
//  llvm_compiler
//
//  Created by Sangmin Shim on 10/10/2018.
//  Copyright © 2018 owen. All rights reserved.
//

#include "llvm/IR/Verifier.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include <string>
#include <vector>
#include <map>

using namespace std;

enum Token_Type //  토큰 타입 지정
{
    EOF_TOKEN=0,    //  파일의 끝을 지정
    NUMERIC_TOKEN,  //  숫자 타입의 토큰
    IDENTIFIER_TOKEN,   //  식별자 토큰
    PARAN_TOKEN,    // 괄호 토큰
    DEF_TOKEN   // 함수 정의임을 지정하는 토큰
};

class BaseAST;

static BaseAST* identifier_parser();    //  identifier()_parser prototype
static BaseAST* numeric_parser();   //  numeric_parser() prototype
static BaseAST* paran_parser(); //  paran_parser() prototype
static BaseAST* expression_parser();    //  expression_parser() prototype
static BaseAST* binary_op_parser(int old_prec, BaseAST *LHS);   //  binary_op_parser(int old_prec, BaseAST *LHS) prototype

static llvm::LLVMContext context;   //  LLVM context
static int numeric_val; //  상수를 저장하기 위한 변수
static FILE *source;    //  소스코드 파일 포인터
static string identifier_string;    //  식별자의 이름을 저장하기 위한 변수
static int current_token;   //  lexer 가 전달한 현재 토큰을 저장하는 변수
static map<char, int> operator_precedence;  //  연산자 우선순위를 전역으로 저장한다.
static llvm::Module* module_Ob; //  LLVM Module Object
static llvm::IRBuilder<> builder(context);  //  LLVM IR Builder
static map<string, llvm::Value*> named_Values;  //  심볼 테이블과 같이 현재 범위에서 정의한 모든 변수를 추적하는 변수

/**
 *  Default AST(Abstract Syntax Tree, 추상 구문 트리)
 */
class BaseAST   // 표현식을 파싱하기 위한 기반 AST
{
public:
    //    virtual ~BaseAST();
    virtual llvm::Value* codeGen()=0;
};

class VariableAST:public BaseAST    //  변수 표현식을 위한 AST
{
    string var_name;

public:
    VariableAST(string &name):var_name(name) {}
    virtual llvm::Value* codeGen();
};

class NumericAST:public BaseAST //  숫자 표현식을 위한 AST
{
    int numeric_val;

public:
    NumericAST(int val):numeric_val(val) {}
    virtual llvm::Value* codeGen();
};

class BinaryAST:public BaseAST  //  이항 연산자를 포함하는 표현식을 위한 AST, LHS 와 RHS 는 각각 이항 표현식에서 좌변 및 우변을 저장하기 위한 객체들이다.
{
    string bin_operator;
    BaseAST *LHS, *RHS;

public:
    BinaryAST(string op, BaseAST *lhs, BaseAST *rhs):bin_operator(op), LHS(lhs), RHS(rhs) {}
    virtual llvm::Value* codeGen();
};

class FunctionDeclAST   //  함수 선언을 위한 AST
{
    string func_name;
    vector<string> arguments;

public:
    FunctionDeclAST(const string &name, const std::vector<string> &args):func_name(name), arguments(args) {}
    virtual llvm::Function* codeGen();
};

class FunctionDefnAST   //  함수 정의를 위한 AST
{
    FunctionDeclAST *func_decl;
    BaseAST* body;

public:
    FunctionDefnAST(FunctionDeclAST *prototype, BaseAST *funcBody):func_decl(prototype), body(funcBody) {}
    virtual llvm::Function* codeGen();
};

class FunctionCallAST:public BaseAST {  //  함수 호출을 위한 AST
    string function_callee;
    vector<BaseAST*> function_arguments;

public:
    FunctionCallAST(const string &callee, vector<BaseAST*> &args):function_callee(callee), function_arguments(args) {}
    virtual llvm::Value* codeGen();
};

/**
 Lexer
 소스코드를 얻어와서 토큰으로 나눠주는 함수
 */
static int get_token()
{
    static int lastChar=' ';

    while(isspace(lastChar))    //  공백이 아닐때 까지 가져온다.(코드가 나타날때 까지 반복한다.)
    {
        lastChar=fgetc(source);
    }

    if(isalpha(lastChar))
    {
        identifier_string=lastChar;

        while(isalnum((lastChar=fgetc(source))))  // 문자나 숫자가 끝날때 까지 반복한다.(코드가 끝날때 까지 반복한다.)   코드 한줄을 identifier_string에 저장
        {
            identifier_string+=lastChar;
        }

        if(identifier_string=="def")    // def 가 나타나면 함수 토큰 return
        {
            return DEF_TOKEN;
        }

        return  IDENTIFIER_TOKEN;
    }

    if(isdigit(lastChar))   //  코드가 숫자일경우에 대한 처리
    {
        std::string numStr;

        do{
            numStr+=lastChar;
            lastChar=fgetc(source);
        }while (isdigit(lastChar));

        numeric_val=strtod(numStr.c_str(), 0);
        return NUMERIC_TOKEN;
    }

    if(lastChar=='#')   //  ??
    {
        do
        {
            lastChar=fgetc(source);
        }
        while(lastChar!=EOF && lastChar!='\n' && lastChar!='\r');

        if(lastChar!=EOF)
        {
            return get_token();
        }
    }

    if(lastChar==EOF)   // 파일의 끝일 경우 파일의 끝 토큰 return
    {
        return EOF_TOKEN;
    }

    int thisChar=lastChar;
    lastChar=fgetc(source);

    return thisChar;    //  문자 반환
}

/**
 다음 토큰을 가져오는 함수
 */
static int next_token()
{
    current_token=get_token();

    return current_token;
}

/**
 lexer 가 구분한 토큰 종류에 따르는 파싱 함수를 호출할 수 있게 범용 파싱 함수를 정의한다.
 */
static BaseAST* base_parser()
{
    switch (current_token) {
        case IDENTIFIER_TOKEN:
            return identifier_parser();
        case NUMERIC_TOKEN:
            return numeric_parser();
        case '(':
            return paran_parser();
        default:
            return 0;
    }
}

/**
 숫자 표현식을 위한 parser 함수를 다음과 같이 정의한다.
 */
static BaseAST* numeric_parser()
{
    BaseAST *result=new NumericAST(numeric_val);
    next_token();

    return result;
}

/**
 식별자 표현식을 위한 함수를 정의한다. 이 함수는 변수 참조나 함수 호출일 수도 있다는 점을 기억한다. 이는 다음 토큰이 '(' 인지 여부를 확인해 구분할수 있다.
 */
static BaseAST* identifier_parser()
{
    string idName=identifier_string;
    next_token();

    if(current_token!='(')
    {
        return new VariableAST(idName);
    }

    next_token();

    vector<BaseAST*> args;

    if(current_token!=')')
    {
        while(1)
        {
            BaseAST* arg=expression_parser();

            if(!arg)
            {
                return 0;
            }

            args.push_back(arg);

            if(current_token==')')
            {
                break;
            }

            if(current_token!=',')
            {
                return 0;
            }

            next_token();
        }
    }

    next_token();

    return new FunctionCallAST(idName, args);
}

/**
 함수 선언을 위한 함수를 정의한다.
 */
static FunctionDeclAST* func_decl_parser()
{
    if(current_token!=IDENTIFIER_TOKEN)
    {
        return 0;
    }

    string fnName=identifier_string;
    next_token();

    if(current_token!='(')
    {
        return 0;
    }

    vector<string> function_argument_names;

    while(next_token()==IDENTIFIER_TOKEN)
    {
        function_argument_names.push_back(identifier_string);
    }

    if(current_token!=')')
    {
        return 0;
    }

    next_token();

    return new FunctionDeclAST(fnName, function_argument_names);
}

/**
 함수 정의를 위한 parser 를 정의한다.
 */
static FunctionDefnAST* func_defn_parser()
{
    next_token();
    FunctionDeclAST *decl=func_decl_parser();

    if(decl==0)
    {
        return 0;
    }

    if(BaseAST *body=expression_parser())
    {
        return new FunctionDefnAST(decl, body);
    }

    return 0;
}

static BaseAST* expression_parser()
{
    BaseAST *LHS=base_parser();

    if(!LHS)
    {
        return 0;
    }

    return binary_op_parser(0, LHS);
}

/**
 연산자 우선순위를 정의하는 함수 정의
 */
static void init_precedence()
{
    operator_precedence['-']=1;
    operator_precedence['+']=2;
    operator_precedence['/']=3;
    operator_precedence['*']=4;
}

/**
 연산자 우선순위를 return 하는 함수 정의
 */
static int getBinOpPrecedence()
{
    if(!isascii(current_token))
    {
        return -1;
    }

    int tokPrec=operator_precedence[current_token];

    if(tokPrec<=0)
    {
        return -1;
    }

    return tokPrec;
}

/**
 이항 연산자를 파싱하는 함수를 정의한다.
 */
static BaseAST* binary_op_parser(int old_prec, BaseAST *LHS)
{
    while(1)
    {
        int operator_prec=getBinOpPrecedence();

        if(operator_prec<old_prec)
        {
            return LHS;
        }

        int binOp=current_token;
        next_token();
        BaseAST* rhs=base_parser();

        if(!rhs)
        {
            return 0;
        }

        int next_prec=getBinOpPrecedence();

        if(operator_prec<next_prec)
        {
            rhs=binary_op_parser(operator_prec+1, rhs);

            if(rhs==0)
            {
                return 0;
            }
        }

        LHS=new BinaryAST(to_string(binOp), LHS, rhs);
    }
}

/**
 괄호에 대한 파서를 정의한다.
 */
static BaseAST* paran_parser()
{
    next_token();
    BaseAST* v=expression_parser();

    if(!v)
    {
        return 0;
    }

    if(current_token!=')')
    {
        return 0;
    }

    return v;
}

/**
 IR 을 만드는 Code Generator
 parser 함수의 wrapper 역활을 하는 최상위 함수를 정의한다.
 */
static void handleDefn()
{
    if(FunctionDefnAST *f=func_defn_parser())
    {
        f->codeGen();
    } else
    {
        next_token();
    }
}

static FunctionDefnAST *top_level_parser() {
    if (BaseAST *E = expression_parser()) {
        FunctionDeclAST *Func_Decl =
        new FunctionDeclAST("", std::vector<std::string>());
        return new FunctionDefnAST(Func_Decl, E);
    }
    return 0;
}

/**
 parser 함수의 wrapper 역활을 하는 최상위 함수를 정의한다.
 */
static void handleTopExpression()
{

    if(FunctionDefnAST *f=top_level_parser())
    {
        f->codeGen();
    } else
    {
        next_token();
    }
}

/**
 파싱을 위한 드라이버 정의
 */
static void driver()
{
    while(1)
    {
        switch (current_token) {
            case EOF_TOKEN:
                return;
            case ';':
                next_token();
                break;
            case DEF_TOKEN:
                handleDefn();
                break;
            default:
                handleTopExpression();
                break;
        }
    }
}

/**
 숫자 값을 위한 코드를 생성하는 함수를 정의한다.
 */
llvm::Value* NumericAST::codeGen()
{
    return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), numeric_val);
}

/**
 변수 표현식을 생성하는 함수는 다음과 같이 정의한다.
 */
llvm::Value* VariableAST::codeGen()
{
    llvm::Value *v=named_Values[var_name];

    return v? v : 0;
}

/**
 이항 표현식을 생성하는 함수를 다음과 같이 정의
 */
llvm::Value* BinaryAST::codeGen()
{
    llvm::Value *left= LHS->codeGen();
    llvm::Value *right=RHS->codeGen();

    if(left==0||right==0)
    {
        return 0;
    }

    switch(atoi(bin_operator.c_str()))
    {
        case '+':
            return builder.CreateAdd(left, right, "addtmp");
        case '-':
            return builder.CreateSub(left, right, "subtmp");
        case '*':
            return builder.CreateMul(left, right, "multmp");
        case '/':
            return builder.CreateUDiv(left, right, "divtmp");
        default:
            return 0;
    }
}

/**
 함수 호출을 생성하기 위한 함수 정의
 */
llvm::Value* FunctionCallAST::codeGen()
{
    llvm::Function *calleeF=module_Ob->getFunction(function_callee);
    vector<llvm::Value*> argsV;

    for(unsigned int i=0;i<function_arguments.size();i++)
    {
        argsV.push_back(function_arguments[i]->codeGen());

        if(argsV.back()==0)
        {
            return 0;
        }
    }

    return builder.CreateCall(calleeF, argsV, "calltmp");
}

/**
 함수 선언을 생성하기 위한 함수 정의
 */
llvm::Function* FunctionDeclAST::codeGen()
{
    vector<llvm::Type*> integers(arguments.size(), llvm::Type::getInt32Ty(context));
    llvm::FunctionType *ft=llvm::FunctionType::get(llvm::Type::getInt32Ty(context), integers, false);
    llvm::Function *f=llvm::Function::Create(ft, llvm::Function::ExternalLinkage, func_name, module_Ob);

    if(f->getName()!=func_name)
    {
        f->eraseFromParent();
        f=module_Ob->getFunction(func_name);

        if(!f->empty())
        {
            return 0;
        }

        if(f->arg_size()!=arguments.size())
        {
            return 0;
        }
    }

    llvm::Function::arg_iterator arg_it=f->arg_begin();
    for(unsigned int idx=0;idx<arguments.size(); arg_it++, idx++)
    {
        arg_it->setName(arguments[idx]);
        named_Values[arguments[idx]]=arg_it;
    }

    return f;
}

/**
 함수 정의를 생성하기 위한 함수 정의
 */
llvm::Function* FunctionDefnAST::codeGen()
{
    named_Values.clear();

    llvm::Function *theFunction=func_decl->codeGen();

    if(theFunction==0)
    {
        return 0;
    }

    llvm::BasicBlock *basicBlock=llvm::BasicBlock::Create(context, "enrty", theFunction);
    builder.SetInsertPoint(basicBlock);

    if(llvm::Value *retVal=body->codeGen())
    {
        builder.CreateRet(retVal);
        llvm::verifyFunction(*theFunction);

        return theFunction;
    }

    theFunction->eraseFromParent();

    return 0;
}

int main(int argc, char* argv[])
{
    init_precedence();  //  연산자 우선순위 초기화
    source=fopen(argv[1], "r"); //  argv[1] 의 path로 파일 포인터 오픈

    if(source==0)   // 오픈의 실패 했을경우 예외 처리
    {
        printf("Could not open File!!\n");
    }

    next_token();   //  다음 토큰으로 이동
    module_Ob=new llvm::Module("my compiler", context); // LLVM Module Object 변수 초기화
    driver();   //  MainLoop 함수인 driver 호출, 여기서 실질적인 토큰에 따른 handler 동작
    module_Ob->print(llvm::outs(), nullptr);    //  LLVM IR 코드가 출력되는것들을 콘솔로 출력함

    return 0;
}
