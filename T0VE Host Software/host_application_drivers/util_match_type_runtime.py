

from typing import Any


def match_type(value: Any, example: Any) -> bool:
    '''
    Ensure the the type of `value` matches the type of `example` at ALL levels
    Can type check:
        - data primitives
        - dictionaries/nested dictionaries (ensures keys match and values of those keys match)
        - lists/nested lists (ensures element types match)
        - tuples/nested tuples (ensures element types match)
    '''
    # if the example is a type, create an instance of it as a reference
    if isinstance(example, type):
        example = example()
    
    # easy error path--if the types don't match at the top level
    if not isinstance(value, type(example)):
        return False

    # if the value is a dictionary, we need to check the keys and values of the dictionary
    if isinstance(value, dict):
        # check that the keys match
        if value.keys() != example.keys():
            return False

        # go through all the shared keys and make sure the types match
        for key in value.keys():
            if not match_type(value[key], example[key]):
                return False
        return True

    # if the value is a list or tuple, check that sizes are equal and match types of all entries
    if isinstance(value, (list, tuple)):
        if len(value) != len(example):
            return False
        for i in range(len(value)):
            if not match_type(value[i], example[i]):
                return False
        return True 

    # at this point, we've done best effort type checking for containers
    # and our first type check will filter out data primitives
    # so we can just return true
    return True
