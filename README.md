## LuaJIT Remake Project 

*This is a work-in-progress.*

The ultimate goal of this project is to implement a multi-tier method-JIT for Lua.

We employ an unique approach where the interpreter and the JIT tiers are automatically generated from a semantical description of the bytecodes. We believe this will ultimately result in less engineering cost, cleaner and more maintainable code, as well as the *generalizability* to support other languages.
 
Currently we have implemented:
* A feature-complete Lua 5.1 interpreter, which is automatically generated from a [semantical description of the LuaJIT bytecodes](annotated/bytecodes). 
* A completely re-engineered Lua runtime. For example, our implementation of the Lua table uses hidden class, instead of a naive hash table as in Lua/LuaJIT.

The work for implementing the JIT tiers is ongoing...

#### License

[Apache 2.0](https://www.apache.org/licenses/LICENSE-2.0).

