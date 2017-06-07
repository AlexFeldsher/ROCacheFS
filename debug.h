//
// Created by alex on 4/23/17.
//

#ifndef UTHREADS_DEBUG_H
#define UTHREADS_DEBUG_H

#ifndef NDEBUG
	#include <iostream>
	#define DEBUG(x) do {std::cerr << __FILE__ << "::" << __LINE__ << "::" << __func__ << ": " << x << std::endl;} while(0)
#else
	#define DEBUG(x) (void(0))
#endif

#endif //UTHREADS_DEBUG_H
