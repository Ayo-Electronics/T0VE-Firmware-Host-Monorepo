from typing import Any, Dict, Tuple

class FlatDict:
    @staticmethod
    def flatten(nested: Dict[Any, Any]) -> Dict[Tuple[Any, ...], Any]:
        flat = {}
        for key, value in nested.items():
            if isinstance(value, dict):
                children = FlatDict.flatten(value)
                for child_key, child_value in children.items():
                    flat[(key, *child_key)] = child_value
            else:
                flat[(key,)] = value
        return flat

    @staticmethod
    def unflatten(flat: Dict[Tuple[Any, ...], Any]) -> Dict[Any, Any]:
        unflat = {}
        for path, value in flat.items():
            d = unflat
            for key in path[:-1]:
                if key not in d:
                    d[key] = {}
                d = d[key]
            d[path[-1]] = value
        return unflat

    @staticmethod
    def set_with_path(nested: Dict[Any, Any], path: Tuple[Any, ...], value: Any) -> None:
        d = nested
        for key in path[:-1]:
            d = d[key]
        d[path[-1]] = value

    @staticmethod
    def get_with_path(nested: Dict[Any, Any], path: Tuple[Any, ...]) -> Any:
        d = nested
        for key in path:
            d = d[key]
        return d

    @staticmethod
    def has_with_path(nested: Dict[Any, Any], path: Tuple[Any, ...]) -> bool:
        d = nested
        for key in path:
            if key not in d:
                return False
            d = d[key]
        return True

    @staticmethod
    def delete_with_path(nested: Dict[Any, Any], path: Tuple[Any, ...]) -> None:
        d = nested
        for key in path[:-1]:
            d = d[key]
        del d[path[-1]]