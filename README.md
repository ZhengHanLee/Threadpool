This is a simple C library for creating and managing a thread pool. The library provides functionality to submit fork/join tasks to the thread pool and obtain the results through futures. Each worker will take a task from a global list of tasks to execute them concurrently. Mutex locks are used to signal conditionals for task procession and allow each thread to be utilized efficiently.

The library consists of the following main components:

    Thread Pool: Represents a pool of threads that can execute fork/join tasks concurrently.

    Fork/Join Task: A function pointer representing a task that can be submitted to the thread pool for parallel execution.

    Future: Represents the result of a fork/join task. It includes information about the task's status and provides a mechanism to retrieve the result once the task is completed.
