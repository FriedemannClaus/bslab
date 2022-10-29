//
//  test-myfs.cpp
//  testing
//
//  Created by Oliver Waldhorst on 15.12.17.
//  Copyright © 2017 Oliver Waldhorst. All rights reserved.
//

#include "../catch/catch.hpp"

#include "tools.hpp"
#include "myfs.h"
#include "myinmemoryfs.h"

// TODO: Implement your helper functions here!
TEST_CASE("Test","Test1"){
    MyInMemoryFS fs=MyInMemoryFS();
    REQUIRE(fs.test()==42);
}

TEST_CASE("Files","fuseMknod"){
    MyInMemoryFS fs=MyInMemoryFS();
    fs.fuseInit(fuse_conn_info *conn);
}
