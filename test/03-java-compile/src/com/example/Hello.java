package com.example;

public class Hello {
    public static void main(String[] args) {
        System.out.println("Hello from Gantt!");
        if (args.length > 0) {
            System.out.println("Arguments: " + String.join(", ", args));
        }
    }
    
    public static String greet(String name) {
        return "Hello, " + name + "!";
    }
}
