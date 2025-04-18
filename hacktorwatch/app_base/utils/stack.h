/*
* Licensed to the Apache Software Foundation (ASF) under one
* or more contributor license agreements.  See the NOTICE file
* distributed with this work for additional information
* regarding copyright ownership.  The ASF licenses this file
* to you under the Apache License, Version 2.0 (the
* "License"); you may not use this file except in compliance
* with the License.  You may obtain a copy of the License at
*
*  http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing,
* software distributed under the License is distributed on an
* "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
* KIND, either express or implied.  See the License for the
* specific language governing permissions and limitations
* under the License.
*/

#ifndef H_CTX_STACK_H_
#define H_CTX_STACK_H_

#include <stdbool.h>
#include "hacktorwatch/common.h"

#define CTX_STACK_HEAD(stack, ctx) do {                      \
        stack.curr = ctx;                                    \
} while(0);

#define CTX_PUSH(stack, ctx) do  {                           \
    stack.prev = (const struct ctx_node_s *)stack.curr;      \
    stack.curr = ctx;                                        \
} while(0);

#define CTX_POP(stack, ctx)  do {                            \
    ctx = (struct ctx_s *)stack.curr;                        \
    stack.curr = (const struct ctx_s *)stack.prev;           \
} while(0);                                                  \

#endif