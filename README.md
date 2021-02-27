# A lockless MPSC queue

A simple C11 implementation of Vyukov multi-producer, single-consumer lockless queue[1].

## Build & Use

```shell
make # Build example app and run
```

This is a single-header library, to be dropped and used in your project.

## Properties

- Multi-producer: multiple threads can write concurrently.
  Insertion is thread safe and costs one atomic exchange.

- Single-consumer: only one thread can safely remove nodes from the queue.

- Unbounded: The queue is a linked-list and does not limit the number of elements.

- Wait-free writes: writers will never wait for queue state sync when enqueuing.

- Wait-free peeks: The reader does not wait to see is a node is available in the queue.
  Peeking takes a bounded number of instructions. There is however no removal
  forward-guarantee, as it relies on other threads progressing.

- Intrusive: Queue elements are allocated as part of larger objects.
  Objects are retrieved by offset manipulation.

- per-producer FIFO: Elements in the queue are kept in the order their producer
  inserted them. The consumer retrieves them in the same insertion order. When
  multiple producers insert at the same time, either will proceed.

This queue is well-suited for message passing between threads,
where any number of thread can insert a message and a single
thread is meant to receive and process them.

It could be used to implement the Actor concurrency model.

**Note: this queue is serializable but not linearizable.** After a series of insertion,
the queue state remains consistent and the insertion order is compatible with their precedence.
However, because one insertion consists in two separate memory transaction, the queue
state can be found inconsistent *within* the series.

This has important implication regarding the concurrency environment this queue can
be used with. **One must ensure that producer threads cannot be cancelled when
inserting elements in the queue. Either cooperative threads should be used or insertions
should be done outside cancellable sections.**

## Benchmark

The test application serves both as a validation for the implementation and as a benchmark.
Another possible implementation of an MPSC queue requiring only one Compare-And-Swap (CAS) per
insertion is based on a Treiber stack [2], being reversed during element removal.

With low number of threads, on x86 both queues are shown to be competitive. As thread numbers grow
however, an exchange operation can scale, while CAS will not.

## References

1. http://www.1024cores.net/home/lock-free-algorithms/queues/intrusive-mpsc-node-based-queue

2. R. K. Treiber. Systems programming: Coping with parallelism.
   Technical Report RJ 5118, IBM Almaden Research Center, April 1986.

