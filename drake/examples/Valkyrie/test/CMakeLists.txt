
if (LONG_RUNNING_TESTS)
  add_matlab_test(NAME examples/Valkyrie/test/runValkyrieFastWalking COMMAND runValkyrieFastWalking PROPERTIES TIMEOUT 1500)
  add_matlab_test(NAME examples/Valkyrie/test/runValkyrieTurning COMMAND runValkyrieTurning PROPERTIES TIMEOUT 1500)
  add_matlab_test(NAME examples/Valkyrie/test/runValkyrieTurning270 COMMAND runValkyrieTurning270 PROPERTIES TIMEOUT 1500)
endif()
